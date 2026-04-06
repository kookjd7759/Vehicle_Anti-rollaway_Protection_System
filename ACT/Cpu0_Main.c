#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "IfxScuWdt.h"
#include "IfxStm.h"
#include "Bsp.h"

#include "can_act.h"
#include "motor_ctrl.h"
#include "drive_mode.h"
#include "mpu6050.h"

IFX_ALIGN(4) IfxCpu_syncEvent cpuSyncEvent = 0;

void core0_main(void)
{
    IfxCpu_enableInterrupts();
    /* !!WATCHDOG0 AND SAFETY WATCHDOG ARE DISABLED HERE!!
     * Enable the watchdogs and service them periodically if it is required
     */
    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword());

    /* Wait for CPU sync event */
    IfxCpu_emitEvent(&cpuSyncEvent);
    IfxCpu_waitEvent(&cpuSyncEvent, 1);

    /* ── 하드웨어 초기화 ── */
    initMotorPins();
    initEncoderPins();
    //initServoPin();

    initCanAct();
    initMPU6050();

    /* ★ g_stmTicksPerUs는 init_pwm() 이전에 반드시 설정 */
    g_stmTicksPerUs = (uint32)(IfxStm_getFrequency(BSP_DEFAULT_TIMER) / 1000000U);

    init_pwm();



    /* ── 메인 루프 ── */
    while (1)
    {
        updateEncoders();
        updateAccel();
        checkCanBusOff();          /* Bus-Off 감지 → 자동 복구 */
        receive_main_command();
        checkCanTimeout();



        /* CAN 명령 미수신 → 대기 모드 (P단 + 모터 정지) */
        if (g_canCmdValid == 0U)
        {
            enterWaitCommandStep();
            send_act_status();
            continue;
        }

        processBrakeCommand();

        /* 비상 제동 최우선 */
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
