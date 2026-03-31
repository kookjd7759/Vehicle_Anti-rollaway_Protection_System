/**********************************************************************************************************************
 * \file Cpu0_Main.c
 * \brief VAPS MAIN ECU - FreeRTOS + ERU 인터럽트 기반 초음파
 * \board TC375 Lite Kit V2
 *********************************************************************************************************************/
#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "IfxScuWdt.h"
#include "IfxPort.h"

/* FreeRTOS */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* App */
#include "App/sensor_data.h"
#include "App/ultrasonic_isr.h"

IFX_ALIGN(4) IfxCpu_syncEvent g_cpuSyncEvent = 0;

/* ── 전역 변수 정의 ── */
SensorData       g_sensor   = { DRIVER_ABSENT, GEAR_P, DOOR_CLOSE, 0.0f, MOTION_STOPPED };
ControlCommand   g_command  = { RISK_NORMAL, 0, 0, 0, 0, 0, 0 };
SemaphoreHandle_t xSensorMutex;
SemaphoreHandle_t xCommandMutex;
volatile uint8 g_debugLed = 0;

/* ── 태스크 함수 ── */
extern void Task_Sensor(void *param);
extern void Task_Judge(void *param);

/* ── LED 제어 함수 ── */
void Debug_LED_Set(uint8 on)
{
    g_debugLed = on;
    if (on) IfxPort_setPinHigh(&MODULE_P10, 4);
    else    IfxPort_setPinLow(&MODULE_P10, 4);
}

void Board_LED1_Set(uint8 on)
{
    if (on) IfxPort_setPinLow(&MODULE_P00, 5);
    else    IfxPort_setPinHigh(&MODULE_P00, 5);
}

void Board_LED2_Set(uint8 on)
{
    if (on) IfxPort_setPinLow(&MODULE_P00, 6);
    else    IfxPort_setPinHigh(&MODULE_P00, 6);
}

/* ── GPIO 초기화 (초음파 제외) ── */
static void GPIO_Init(void)
{
    IfxPort_setPinModeInput(&MODULE_P02, 3, IfxPort_InputMode_pullUp);  /* 기어 P */
    IfxPort_setPinModeInput(&MODULE_P02, 5, IfxPort_InputMode_pullUp);  /* 기어 R */
    IfxPort_setPinModeInput(&MODULE_P02, 4, IfxPort_InputMode_pullUp);  /* 기어 N */
    IfxPort_setPinModeInput(&MODULE_P02, 6, IfxPort_InputMode_pullUp);  /* 기어 D */
    IfxPort_setPinModeInput(&MODULE_P02, 7, IfxPort_InputMode_pullUp);  /* 도어 */

    IfxPort_setPinModeOutput(&MODULE_P10, 4,
        IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinLow(&MODULE_P10, 4);

    IfxPort_setPinModeOutput(&MODULE_P00, 5,
        IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinHigh(&MODULE_P00, 5);

    IfxPort_setPinModeOutput(&MODULE_P00, 6,
        IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinHigh(&MODULE_P00, 6);
}

/* ── core0_main ── */
void core0_main(void)
{
    IfxCpu_enableInterrupts();
    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword());

    IfxCpu_emitEvent(&g_cpuSyncEvent);
    IfxCpu_waitEvent(&g_cpuSyncEvent, 1);

    GPIO_Init();
    Ultrasonic_Init();   /* ERU 인터럽트 + Trig/Echo 핀 설정 */

    xSensorMutex  = xSemaphoreCreateMutex();
    xCommandMutex = xSemaphoreCreateMutex();

    xTaskCreate(Task_Sensor, "Sensor", 1024, NULL, 3, NULL);
    xTaskCreate(Task_Judge,  "Judge",  1024, NULL, 3, NULL);

    vTaskStartScheduler();

    while (1) { }
}

/* FreeRTOS 필수 콜백 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    while (1)
    {
        IfxPort_togglePin(&MODULE_P00, 6);
        for (volatile int i = 0; i < 100000; i++);
    }
}
