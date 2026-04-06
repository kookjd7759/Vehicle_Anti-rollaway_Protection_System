/**********************************************************************************************************************
 * \file can_handler.c
 *********************************************************************************************************************/

#include "App/can_handler.h"
#include "App/sensor_data.h"
#include "Can/Can/IfxCan_Can.h"
#include "FreeRTOS.h"
#include "task.h"

#define CAN_NODE_ID_MAIN              IfxCan_NodeId_0
#define CAN_BAUDRATE                  500000U
#define CAN_TASK_PERIOD_MS            10U
#define CAN_TX_PERIOD_0X100_MS        50U
#define CAN_TX_PERIOD_0X200_MS        100U
#define CAN_RX_TIMEOUT_MS             300U

#define CAN_ID_MAIN_ACT_CTRL          0x100U
#define CAN_ID_MAIN_CLU_STATUS        0x200U
#define CAN_ID_ACT_FEEDBACK           0x300U

#define CAN_TX_BUFFER_MAIN_ACT_CTRL   0U
#define CAN_TX_BUFFER_MAIN_CLU_STATUS 1U
#define CAN_RX_FIFO0_SIZE             8U
#define CAN_SEND_RETRY_COUNT          5U
#define CAN_SEND_RETRY_DELAY_MS       1U
#define CAN_BUSOFF_RECOVERY_DELAY_MS  50U

static IfxCan_Can g_canModule;
static IfxCan_Can_Node g_canNode;
static IfxCan_Can_NodeConfig g_savedNodeConfig;
static boolean g_canReady = FALSE;
volatile uint32 g_can_busoff_recovery_count = 0U;

volatile boolean g_gear_override_active = FALSE;
volatile GearState g_gear_override_state = GEAR_P;
volatile boolean g_can_init_ok = FALSE;
volatile boolean g_can_act_feedback_timeout = TRUE;
volatile uint8 g_can_bus_off_debug = 0U;
volatile uint32 g_can_last_rx_tick = 0U;
volatile uint32 g_can_last_speed_x100 = 0U;
volatile uint8 g_can_last_brake_state = BRAKE_CMD_RELEASE;
volatile sint16 g_can_last_accel_x = 0;
volatile sint16 g_can_last_accel_y = 0;
volatile sint16 g_can_last_accel_z = 0;
volatile uint32 g_can_tx100_count = 0U;
volatile uint32 g_can_tx200_count = 0U;
volatile uint32 g_can_rx300_count = 0U;
volatile uint32 g_can_tx_error_count = 0U;
volatile uint8 g_can_last_tx100_brake_cmd = BRAKE_CMD_RELEASE;
volatile uint8 g_can_last_tx100_gear = GEAR_P;
volatile uint8 g_can_last_tx200_risk = RISK_NORMAL;
volatile uint8 g_can_last_tx200_driver_present = 0U;
volatile uint8 g_can_last_tx200_door = 0U;
volatile uint8 g_can_last_tx200_gear = GEAR_P;

static uint8 can_encode_gear(GearState gear)
{
    switch (gear)
    {
    case GEAR_R:
        return 1U;
    case GEAR_N:
        return 2U;
    case GEAR_D:
        return 3U;
    case GEAR_P:
    default:
        return 0U;
    }
}

static uint8 can_encode_driver_present(DriverState driver)
{
    return (driver == DRIVER_SEATED) ? 1U : 0U;
}

static uint8 can_encode_door(DoorState door)
{
    return (door == DOOR_OPEN) ? 1U : 0U;
}

static uint8 can_normalize_brake_state(uint8 value)
{
    if (value > (uint8)BRAKE_CMD_FORCE)
        return (uint8)BRAKE_CMD_RELEASE;

    return value;
}

static boolean can_is_auto_brake_risk(RiskLevel risk)
{
    return (risk == RISK_D_BRAKE) ||
           (risk == RISK_R_BRAKE) ||
           (risk == RISK_ROLLAWAY_BRAKE);
}

static uint32 can_pack_u32_le(uint8 byte0, uint8 byte1, uint8 byte2, uint8 byte3)
{
    return ((uint32)byte0) |
           ((uint32)byte1 << 8) |
           ((uint32)byte2 << 16) |
           ((uint32)byte3 << 24);
}

/* [BUG-4 수정] can_unpack_u32_le() 삭제: AURIX LE이므로 rxData[0] 직접 사용 */

static void can_apply_act_feedback(uint32 speedX100, uint8 brakeState,
                                   sint16 accelX, sint16 accelY, sint16 accelZ)
{
    if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(2)) == pdTRUE)
    {
        g_sensor.speed_kmh = (float)speedX100;
        g_sensor.accel_x = accelX;
        g_sensor.accel_y = accelY;
        g_sensor.accel_z = accelZ;
        g_sensor.act_brake_state = (BrakeCommand)can_normalize_brake_state(brakeState);
        g_sensor.act_feedback_alive = TRUE;
        xSemaphoreGive(xSensorMutex);
    }
}

static void can_apply_act_timeout(void)
{
    if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(2)) == pdTRUE)
    {
        g_sensor.speed_kmh = 0.0f;
        g_sensor.accel_x = 0;
        g_sensor.accel_y = 0;
        g_sensor.accel_z = 0;
        g_sensor.act_brake_state = BRAKE_CMD_RELEASE;
        g_sensor.act_feedback_alive = FALSE;
        xSemaphoreGive(xSensorMutex);
    }
}

/* 브레이크 RELEASE 전환 후 Main 기어 보고값을 P로 고정한다. */
static void can_apply_gear_override(GearState gear)
{
    g_gear_override_state = gear;
    g_gear_override_active = TRUE;

    if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(2)) == pdTRUE)
    {
        g_sensor.gear = gear;
        xSemaphoreGive(xSensorMutex);
    }
}

static IfxCan_Status can_send_message(IfxCan_Message *message, uint32 *data)
{
    IfxCan_Status status = IfxCan_Status_notSentBusy;
    uint32 retry = 0U;

    while (retry < CAN_SEND_RETRY_COUNT)
    {
        status = IfxCan_Can_sendMessage(&g_canNode, message, data);
        if (status != IfxCan_Status_notSentBusy)
            return status;

        retry++;
        vTaskDelay(pdMS_TO_TICKS(CAN_SEND_RETRY_DELAY_MS));
    }

    return status;
}

static void can_send_main_act_ctrl(const SensorData *sensor, const ControlCommand *command)
{
    IfxCan_Message message;
    uint32 txData[2] = {0U, 0U};
    boolean autoBrakeCommand = can_is_auto_brake_risk(command->risk_level);
    GearState txGearState = sensor->gear;
    uint8 brakeCmd = (uint8)command->brake_cmd;
    boolean shiftToParkOnRelease = (brakeCmd == (uint8)BRAKE_CMD_RELEASE) &&
                                   (g_can_last_tx100_brake_cmd != (uint8)BRAKE_CMD_RELEASE);
    uint8 gear;

    /* 자동제동 중에는 FORCE를 유지하고, P 전환은 RELEASE 시점에만 적용한다. */
    if (autoBrakeCommand == TRUE)
    {
        brakeCmd = (uint8)BRAKE_CMD_FORCE;
    }
    else if (shiftToParkOnRelease == TRUE)
    {
        txGearState = GEAR_P;
    }
    else if (g_gear_override_active == TRUE)
    {
        txGearState = g_gear_override_state;
    }

    gear = can_encode_gear(txGearState);

    IfxCan_Can_initMessage(&message);
    message.bufferNumber = CAN_TX_BUFFER_MAIN_ACT_CTRL;
    message.messageId = CAN_ID_MAIN_ACT_CTRL;
    message.dataLengthCode = IfxCan_DataLengthCode_2;

    txData[0] = can_pack_u32_le(brakeCmd, gear, 0U, 0U);

    if (can_send_message(&message, txData) == IfxCan_Status_ok)
    {
        g_can_last_tx100_brake_cmd = brakeCmd;
        g_can_last_tx100_gear = gear;

        if (shiftToParkOnRelease == TRUE)
            can_apply_gear_override(GEAR_P);

        g_can_tx100_count++;
    }
    else
    {
        g_can_tx_error_count++;
    }
}

static void can_send_main_clu_status(const SensorData *sensor, const ControlCommand *command)
{
    IfxCan_Message message;
    uint32 txData[2] = {0U, 0U};
    uint8 risk = (uint8)command->risk_level;
    uint8 driverPresent = can_encode_driver_present(sensor->driver);
    uint8 doorState = can_encode_door(sensor->door);
    GearState txGearState = sensor->gear;
    uint8 gear;

    if (g_gear_override_active == TRUE)
        txGearState = g_gear_override_state;

    gear = can_encode_gear(txGearState);

    IfxCan_Can_initMessage(&message);
    message.bufferNumber = CAN_TX_BUFFER_MAIN_CLU_STATUS;
    message.messageId = CAN_ID_MAIN_CLU_STATUS;
    message.dataLengthCode = IfxCan_DataLengthCode_4;

    txData[0] = can_pack_u32_le(risk, driverPresent, doorState, gear);

    g_can_last_tx200_risk = risk;
    g_can_last_tx200_driver_present = driverPresent;
    g_can_last_tx200_door = doorState;
    g_can_last_tx200_gear = gear;

    if (can_send_message(&message, txData) == IfxCan_Status_ok)
        g_can_tx200_count++;
    else
        g_can_tx_error_count++;
}

static void can_process_rx_feedback(TickType_t now)
{
    while (IfxCan_Can_getRxFifo0FillLevel(&g_canNode) > 0U)
    {
        IfxCan_Message rxMessage;
        uint32 rxData[2] = {0U, 0U};

        IfxCan_Can_initMessage(&rxMessage);
        rxMessage.readFromRxFifo0 = TRUE;
        IfxCan_Can_readMessage(&g_canNode, &rxMessage, rxData);

        if ((rxMessage.messageId == CAN_ID_ACT_FEEDBACK) &&
            (rxMessage.dataLengthCode >= IfxCan_DataLengthCode_8))
        {
            /* Byte0~3=speed, Byte4=brake, Byte5~7=accel xyz (AURIX LE) */
            uint32 speedX100 = rxData[0];
            uint8 brakeState = (uint8)(rxData[1] & 0xFFU);
            uint8 rawByte5 = (uint8)((rxData[1] >> 8) & 0xFFU);
            uint8 rawByte6 = (uint8)((rxData[1] >> 16) & 0xFFU);
            uint8 rawByte7 = (uint8)((rxData[1] >> 24) & 0xFFU);
            sint16 accelX = (sint16)((sint8)rawByte5);
            sint16 accelY = (sint16)((sint8)rawByte6);
            sint16 accelZ = (sint16)((sint8)rawByte7);

            g_can_last_speed_x100 = speedX100;
            g_can_last_brake_state = can_normalize_brake_state(brakeState);
            g_can_last_accel_x = accelX;
            g_can_last_accel_y = accelY;
            g_can_last_accel_z = accelZ;
            g_can_last_rx_tick = (uint32)now;
            g_can_act_feedback_timeout = FALSE;
            g_can_rx300_count++;

            can_apply_act_feedback(speedX100, brakeState, accelX, accelY, accelZ);
        }
    }
}




static void can_setup_filter(void)
{
    IfxCan_Filter filter;

    filter.number = 0U;
    filter.elementConfiguration = IfxCan_FilterElementConfiguration_storeInRxFifo0;
    filter.type = IfxCan_FilterType_classic;
    filter.id1 = CAN_ID_ACT_FEEDBACK;
    filter.id2 = 0x7FFU;
    filter.rxBufferOffset = IfxCan_RxBufferId_0;
    IfxCan_Can_setStandardFilter(&g_canNode, &filter);
}

static boolean can_recover_busoff(void)
{
    g_canReady = FALSE;
    g_can_busoff_recovery_count++;

    if (IfxCan_Can_initNode(&g_canNode, &g_savedNodeConfig) == TRUE)
    {
        can_setup_filter();

        /* 동기화 대기 (타임아웃 포함) */
        {
            uint32 wait = 0U;
            while (IfxCan_Can_isNodeSynchronized(&g_canNode) != TRUE)
            {
                if (++wait > 100000U)
                    return FALSE;
            }
        }

        g_canReady = TRUE;
        return TRUE;
    }

    return FALSE;
}

void CanApp_Init(void)
{
    /* CAN 트랜시버 활성화 (STB = Low) */
    IfxPort_setPinModeOutput(&MODULE_P20, 6,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);
    IfxPort_setPinLow(&MODULE_P20, 6);

    IfxCan_Can_Config canConfig;
    static const IfxCan_Can_Pins canPins = {
        .txPin = &IfxCan_TXD00_P20_8_OUT,
        .txPinMode = IfxPort_OutputMode_pushPull,
        .rxPin = &IfxCan_RXD00B_P20_7_IN,
        .rxPinMode = IfxPort_InputMode_noPullDevice,
        .padDriver = IfxPort_PadDriver_cmosAutomotiveSpeed1
    };

    IfxCan_Can_initModuleConfig(&canConfig, &MODULE_CAN0);
    IfxCan_Can_initModule(&g_canModule, &canConfig);

    IfxCan_Can_initNodeConfig(&g_savedNodeConfig, &g_canModule);
    g_savedNodeConfig.nodeId = CAN_NODE_ID_MAIN;
    g_savedNodeConfig.clockSource = IfxCan_ClockSource_both;
    g_savedNodeConfig.frame.type = IfxCan_FrameType_transmitAndReceive;
    g_savedNodeConfig.frame.mode = IfxCan_FrameMode_standard;
    g_savedNodeConfig.calculateBitTimingValues = TRUE;
    g_savedNodeConfig.baudRate.baudrate = CAN_BAUDRATE;
    g_savedNodeConfig.baudRate.samplePoint = 8000;
    g_savedNodeConfig.baudRate.syncJumpWidth = 3;
    g_savedNodeConfig.txConfig.txMode = IfxCan_TxMode_dedicatedBuffers;
    g_savedNodeConfig.txConfig.dedicatedTxBuffersNumber = 2;
    g_savedNodeConfig.txConfig.txBufferDataFieldSize = IfxCan_DataFieldSize_8;
    g_savedNodeConfig.filterConfig.messageIdLength = IfxCan_MessageIdLength_standard;
    g_savedNodeConfig.filterConfig.standardListSize = 1;
    g_savedNodeConfig.filterConfig.standardFilterForNonMatchingFrames = IfxCan_NonMatchingFrame_reject;
    g_savedNodeConfig.rxConfig.rxMode = IfxCan_RxMode_fifo0;
    g_savedNodeConfig.rxConfig.rxFifo0DataFieldSize = IfxCan_DataFieldSize_8;
    g_savedNodeConfig.rxConfig.rxFifo0Size = CAN_RX_FIFO0_SIZE;
    g_savedNodeConfig.rxConfig.rxFifo0OperatingMode = IfxCan_RxFifoMode_blocking;
    g_savedNodeConfig.pins = &canPins;

    g_can_init_ok = IfxCan_Can_initNode(&g_canNode, &g_savedNodeConfig);

    if (g_can_init_ok == TRUE)
    {
        can_setup_filter();

        while (IfxCan_Can_isNodeSynchronized(&g_canNode) != TRUE)
        {
        }

        g_canReady = TRUE;
    }
    else
    {
        g_canReady = FALSE;
    }
}

void Task_Can(void *param)
{
    TickType_t xLastWake = xTaskGetTickCount();
    TickType_t lastTx100 = xLastWake;
    TickType_t lastTx200 = xLastWake;
    TickType_t lastRx300 = xLastWake;

    /* [BUG-3 수정] 안전한 기본값으로 초기화 (mutex 없는 비보호 복사 제거) */
    SensorData sensorSnapshot = {
        DRIVER_ABSENT, GEAR_P, DOOR_CLOSE, 0.0f, 0, 0, 0,
        MOTION_STOPPED, BRAKE_CMD_RELEASE, FALSE
    };
    ControlCommand commandSnapshot = { RISK_NORMAL, BRAKE_CMD_RELEASE };

    IFX_UNUSED_PARAMETER(param);

    while (1)
    {
        TickType_t now = xTaskGetTickCount();

        if (!g_canReady)
        {
            vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(CAN_TASK_PERIOD_MS));
            continue;
        }

        /* Bus-Off 감지 및 자동 복구 */
        g_can_bus_off_debug = (uint8)IfxCan_Node_getBusOffStatus(g_canNode.node);
        if (g_can_bus_off_debug != 0U)
        {
            vTaskDelay(pdMS_TO_TICKS(CAN_BUSOFF_RECOVERY_DELAY_MS));
            can_recover_busoff();
            vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(CAN_TASK_PERIOD_MS));
            continue;
        }

        can_process_rx_feedback(now);

        if (g_can_act_feedback_timeout == FALSE)
        {
            lastRx300 = (TickType_t)g_can_last_rx_tick;
        }

        if ((now - lastRx300) >= pdMS_TO_TICKS(CAN_RX_TIMEOUT_MS))
        {
            g_can_act_feedback_timeout = TRUE;
            g_can_last_speed_x100 = 0U;
            g_can_last_brake_state = BRAKE_CMD_RELEASE;
            g_can_last_accel_x = 0;
            g_can_last_accel_y = 0;
            g_can_last_accel_z = 0;
            can_apply_act_timeout();
        }

        if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(2)) == pdTRUE)
        {
            sensorSnapshot = g_sensor;
            xSemaphoreGive(xSensorMutex);
        }

        if (xSemaphoreTake(xCommandMutex, pdMS_TO_TICKS(2)) == pdTRUE)
        {
            commandSnapshot = g_command;
            xSemaphoreGive(xCommandMutex);
        }

        if ((now - lastTx100) >= pdMS_TO_TICKS(CAN_TX_PERIOD_0X100_MS))
        {
            can_send_main_act_ctrl(&sensorSnapshot, &commandSnapshot);
            lastTx100 = now;
        }

        if ((now - lastTx200) >= pdMS_TO_TICKS(CAN_TX_PERIOD_0X200_MS))
        {
            can_send_main_clu_status(&sensorSnapshot, &commandSnapshot);
            lastTx200 = now;
        }

        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(CAN_TASK_PERIOD_MS));
    }
}
