#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "IfxScuWdt.h"
#include "IfxStm.h"
#include "Bsp.h"

#include "can_act.h"      /* CAN 송수신 */
#include "motor_ctrl.h"   /* 모터 / 엔코더 / 서보 */
#include "drive_mode.h"   /* 기어 상태머신 */

IFX_ALIGN(4) IfxCpu_syncEvent g_cpuSyncEvent = 0;

void core0_main(void)
{
    IfxCpu_enableInterrupts();
    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword());

    initMotorPins();
    initEncoderPins();
    initServoPin();
    servoHold(SERVO_RELEASE_US, 500U);

    initCanAct();

    g_stmTicksPerUs = (uint32)(IfxStm_getFrequency(BSP_DEFAULT_TIMER) / 1000000U);

    while (1)
    {
        updateEncoders();
        receive_main_command();
        checkCanTimeout();

        if (g_canCmdValid == 0U)
        {
            enterWaitCommandStep();
            send_act_status();
            continue;
        }

        processBrakeCommand();

        if (g_canBrakeCmd == BRAKE_CMD_EMERGENCY)
        {
            applyEmergencyBrakeStep();
        }
        else
        {
            switch (g_gearMode)
            {
            case GEAR_P: enterPModeStep(); break;
            case GEAR_R: runRModeStep();   break;
            case GEAR_D: runDModeStep();   break;
            case GEAR_N: runNModeStep();   break;
            default:     enterPModeStep(); break;
            }
        }

        send_act_status();
    }
}
