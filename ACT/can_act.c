#include "can_act.h"
#include "drive_mode.h"
#include "IfxPort.h"
#include "IfxStm.h"
#include "Bsp.h"
#include "mpu6050.h"

/* ── 전역 정의 ── */
IfxCan_Can      g_mcmcan;
IfxCan_Can_Node g_canNode0;

volatile uint8  g_canBrakeCmd  = BRAKE_CMD_RELEASE;
volatile uint8  g_canGearState = CAN_GEAR_P;
volatile uint8  g_canCmdValid  = 0U;
volatile uint32 g_lastRxTick   = 0U;

volatile uint32 g_vehicleSpeed  = 0U;
volatile uint8  g_brakeStateCan = BRAKE_CMD_RELEASE;

volatile uint8  g_txSuccess = 0U, g_txBusy = 0U, g_txFail = 0U;
volatile uint32 g_txCount   = 0U;
volatile uint8  g_rxNew     = 0U, g_rxFail = 0U;
volatile uint32 g_rxCount   = 0U;
volatile uint32 g_rxData0   = 0U, g_rxData1 = 0U;
volatile uint8  g_canBusOffCount = 0U;

/* ── 노드 설정 저장 (Bus-Off 복구 시 재사용) ── */
static IfxCan_Can_NodeConfig s_canNodeConfig;

/* ── 내부: 수신 필터 설정 ── */
static void initStandardFilter(void)
{
    IfxCan_Filter filter;
    filter.number               = 0;
    /* FIFO0으로 저장 */
    filter.elementConfiguration = IfxCan_FilterElementConfiguration_storeInRxFifo0;
    filter.type                 = IfxCan_FilterType_classic;
    filter.id1                  = CAN_RX_ID;
    filter.id2                  = 0x7FFU;
    filter.rxBufferOffset       = IfxCan_RxBufferId_0;
    IfxCan_Can_setStandardFilter(&g_canNode0, &filter);
}

/* ── CAN 초기화 ── */
void initCanAct(void)
{
    /* CAN 트랜시버 STB 핀 LOW → 정상 동작 모드 */
    IfxPort_setPinModeOutput(CAN_STB_PORT, CAN_STB_PIN,
                             IfxPort_OutputMode_pushPull,
                             IfxPort_OutputIdx_general);
    IfxPort_setPinLow(CAN_STB_PORT, CAN_STB_PIN);

    /* CAN 모듈 초기화 */
    {
        IfxCan_Can_Config canConfig;
        IfxCan_Can_initModuleConfig(&canConfig, &MODULE_CAN0);
        IfxCan_Can_initModule(&g_mcmcan, &canConfig);
    }

    /* CAN 노드 설정 */
    {
        IfxCan_Can_initNodeConfig(&s_canNodeConfig, &g_mcmcan);

        s_canNodeConfig.nodeId            = IfxCan_NodeId_0;

        /* ── 비트 타이밍: MAIN과 동일하게 설정 ── */
        s_canNodeConfig.baudRate.baudrate          = 500000U;
        s_canNodeConfig.baudRate.samplePoint       = 8000U;   /* 80.00% */
        s_canNodeConfig.baudRate.syncJumpWidth     = 3U;
        s_canNodeConfig.clockSource                = IfxCan_ClockSource_both;
        s_canNodeConfig.calculateBitTimingValues   = TRUE;

        s_canNodeConfig.frame.type = IfxCan_FrameType_transmitAndReceive;

        static const IfxCan_Can_Pins canPins = {
            &IfxCan_TXD00_P20_8_OUT, IfxPort_OutputMode_pushPull,
            &IfxCan_RXD00B_P20_7_IN, IfxPort_InputMode_pullUp,
            IfxPort_PadDriver_cmosAutomotiveSpeed1
        };
        s_canNodeConfig.pins = &canPins;

        /* TX: Dedicated Buffer 1개 */
        s_canNodeConfig.txConfig.txMode                   = IfxCan_TxMode_dedicatedBuffers;
        s_canNodeConfig.txConfig.dedicatedTxBuffersNumber = 1U;
        s_canNodeConfig.txConfig.txBufferDataFieldSize    = IfxCan_DataFieldSize_8;
        s_canNodeConfig.txConfig.txEventFifoSize          = 0U;

        /* RX: FIFO0 사용 (8개 버퍼 → 메시지 유실 방지) */
        s_canNodeConfig.rxConfig.rxMode                  = IfxCan_RxMode_fifo0;
        s_canNodeConfig.rxConfig.rxFifo0DataFieldSize    = IfxCan_DataFieldSize_8;
        s_canNodeConfig.rxConfig.rxFifo0Size             = 8U;
        s_canNodeConfig.rxConfig.rxFifo0OperatingMode   = IfxCan_RxFifoMode_blocking;

        /* Non-matching 프레임 거부 */
        s_canNodeConfig.filterConfig.standardListSize = 1U;
        s_canNodeConfig.filterConfig.standardFilterForNonMatchingFrames =
            IfxCan_NonMatchingFrame_reject;

        IfxCan_Can_initNode(&g_canNode0, &s_canNodeConfig);
    }

    /* 수신 필터: 0x100 → FIFO0 */
    initStandardFilter();
}

/* ── Bus-Off 감지 및 자동 복구 ── */
void checkCanBusOff(void)
{
    if (IfxCan_Node_getBusOffStatus(g_canNode0.node) == FALSE)
        return;

    /* Bus-Off 진입 감지 → 카운터 증가 */
    g_canBusOffCount++;
    g_canCmdValid = 0U;   /* 안전: 타임아웃 처리 */

    /* 노드 재초기화 */
    IfxCan_Can_initNode(&g_canNode0, &s_canNodeConfig);
    initStandardFilter();

    /* 버스 동기화 대기 (최대 100ms) */
    uint32 wait = 0U;
    while (IfxCan_Can_isNodeSynchronized(&g_canNode0) != TRUE)
    {
        if (++wait > 100000U) break;
    }
}

/* ── 수신: MAIN → ACT (0x100, FIFO0) ── */
void receive_main_command(void)
{
    IfxCan_Message rxMsg;
    uint32 rxData[2] = {0U, 0U};

    g_rxFail = 0U;
    g_rxNew  = 0U;

    /* FIFO0에 수신된 메시지가 없으면 리턴 */
    if (IfxCan_Can_getRxFifo0FillLevel(&g_canNode0) == 0U)
    {
        g_rxFail = 1U;
        return;
    }

    IfxCan_Can_initMessage(&rxMsg);
    rxMsg.bufferNumber    = 0U;
    rxMsg.frameMode       = IfxCan_FrameMode_standard;
    rxMsg.messageIdLength = IfxCan_MessageIdLength_standard;
    rxMsg.dataLengthCode  = IfxCan_DataLengthCode_2;
    rxMsg.readFromRxFifo0 = TRUE;   /* FIFO0에서 읽음 */

    IfxCan_Can_readMessage(&g_canNode0, &rxMsg, rxData);

    if (rxMsg.messageId == CAN_RX_ID)
    {
        g_rxData0 = rxData[0];
        g_rxData1 = rxData[1];
        g_rxCount++;
        g_rxNew = 1U;

        /* Byte 0: brake_cmd / Byte 1: gear_state */
        g_canBrakeCmd  = (uint8)(rxData[0]        & 0xFFU);
        g_canGearState = (uint8)((rxData[0] >> 8U) & 0xFFU);

        g_canCmdValid = 1U;
        g_lastRxTick  = IfxStm_getLower(BSP_DEFAULT_TIMER);
    }
    else
    {
        g_rxFail = 1U;
    }
}

/* ── 타임아웃 감시: 500ms 미수신 → P단 강제 ── */
void checkCanTimeout(void)
{
    if (g_canCmdValid == 0U) return;

    uint32 now       = IfxStm_getLower(BSP_DEFAULT_TIMER);
    uint32 elapsedUs = (now - g_lastRxTick) / g_stmTicksPerUs;

    if (elapsedUs >= CAN_TIMEOUT_US)
    {
        g_canCmdValid  = 0U;
        g_canBrakeCmd  = BRAKE_CMD_RELEASE;
        g_canGearState = CAN_GEAR_P;
        g_gearMode     = GEAR_P;
    }
}

/* ── 송신: ACT → MAIN (0x300, 8바이트) ── */
void send_act_status(void)
{
    /*
     * Byte 0~3 : speed       (uint32, km/h × 100, little-endian)
     * Byte 4   : brake_state (0:해제 / 1:유지 / 2:긴급)
     * Byte 5   : accel_x     (sint8, g × 100, ±100)
     * Byte 6   : accel_y     (sint8, g × 100, ±100)
     * Byte 7   : accel_z     (sint8, g × 100, ±100)
     */
    IfxCan_Message txMsg;
    uint32 txData[2];
    IfxCan_Status status;
    sint32 timeout = 1000;   /* 10000 → 1000으로 축소 (블로킹 시간 감소) */

    g_txSuccess = 0U; g_txBusy = 0U; g_txFail = 0U;

    updateVehicleSpeed();
    updateBrakeStateCan();

    txData[0] = g_vehicleSpeed;
    txData[1] = ((uint32)g_brakeStateCan      & 0xFFU)
              | ((uint32)(uint8)g_accelX <<  8U)
              | ((uint32)(uint8)g_accelY << 16U)
              | ((uint32)(uint8)g_accelZ << 24U);

    IfxCan_Can_initMessage(&txMsg);
    txMsg.messageId       = CAN_TX_ID;
    txMsg.bufferNumber    = 0U;
    txMsg.frameMode       = IfxCan_FrameMode_standard;
    txMsg.messageIdLength = IfxCan_MessageIdLength_standard;
    txMsg.dataLengthCode  = IfxCan_DataLengthCode_8;
    txMsg.readFromRxFifo0 = FALSE;

    do {
        /* Bus-Off 상태면 즉시 탈출 (무한 대기 방지) */
        if (IfxCan_Node_getBusOffStatus(g_canNode0.node) != FALSE)
        {
            g_txFail = 1U;
            return;
        }
        status = IfxCan_Can_sendMessage(&g_canNode0, &txMsg, txData);
        if (status == IfxCan_Status_ok)         { g_txSuccess = 1U; g_txCount++; return; }
        if (status == IfxCan_Status_notSentBusy)  g_txBusy = 1U;
        timeout--;
    } while (timeout > 0);

    g_txFail = 1U;
}
