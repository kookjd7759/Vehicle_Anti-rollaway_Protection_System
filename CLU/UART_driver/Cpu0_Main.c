/**********************************************************************************************************************
 * \file Cpu0_Main.c
 *********************************************************************************************************************/
#include "Ifx_Types.h"
#include "IfxScuWdt.h"
#include "IfxPort.h"
#include "IfxStm.h"
#include "IfxCpu.h"
#include "uart_msg.h"
#include "Bsp.h"
#include <time.h>
#include <stdlib.h>

IfxCpu_syncEvent cpuSyncEvent = 0;

#define BTN_PORT    &MODULE_P02
#define BTN_PIN     1

void send_random(void)
{
    int warning = rand() % 4;
    int brake = rand() % 4;
    int gear = rand() % 4;
    int door = rand() % 2;
    int driver = rand() % 4;
    int speed = rand() % 100;
    sendData(warning, brake, gear, door, driver, speed);
}

void core0_main(void)
{
    IfxCpu_enableInterrupts();

    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword());

    IfxCpu_emitEvent(&cpuSyncEvent);
    IfxCpu_waitEvent(&cpuSyncEvent, 1);

    Driver_Asc0_Init();
    srand((unsigned int)IfxStm_getLower(&MODULE_STM0));

    Driver_Asc0_Init();

    uint8 prevState = 0;
    IfxPort_setPinModeInput(
            BTN_PORT,
            BTN_PIN,
            IfxPort_InputMode_pullUp
        );

    while (1)
    {
        uint8 currState = IfxPort_getPinState(BTN_PORT, BTN_PIN);

        if (prevState == 1 && currState == 0)
        {
            waitTime(IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, 20));

            if (IfxPort_getPinState(BTN_PORT, BTN_PIN) == 0)
            {
                send_random();

                while (IfxPort_getPinState(BTN_PORT, BTN_PIN) == 0)
                {
                }
            }
        }

        prevState = currState;
    }
}
