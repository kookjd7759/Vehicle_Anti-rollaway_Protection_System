#include "drive_mode.h"
#include "motor_ctrl.h"
#include "IfxStm.h"
#include "Bsp.h"

/* ── 전역 정의 ── */
volatile GearMode g_gearMode     = GEAR_P;
volatile uint8    g_driveState   = 0U;
volatile uint8    g_brakeApplied = 0U;
volatile uint8    g_vehicleMoved = 0U;
volatile uint32   g_stmTicksPerUs = 100U;

static volatile uint8  g_servoBrakeCmd  = 0U;
static volatile uint8  g_directionForward = 1U;
static volatile uint8  g_nModeInitDone  = 0U;
static volatile uint32 g_prevAvgPulse   = 0U;
static volatile uint32 g_prevSpeedTick  = 0U;

GearMode convertCanGearToMode(uint8 canGear)
{
    switch (canGear)
    {
    case CAN_GEAR_P: return GEAR_P;
    case CAN_GEAR_R: return GEAR_R;
    case CAN_GEAR_N: return GEAR_N;
    case CAN_GEAR_D: return GEAR_D;
    default:         return GEAR_P;
    }
}

void updateVehicleSpeed(void)
{
    /* g_stmTicksPerUs가 0이면 나눗셈 오류 방지 */
    if (g_stmTicksPerUs == 0U) return;

    uint32 now       = IfxStm_getLower(BSP_DEFAULT_TIMER);
    uint32 ticks     = now - g_prevSpeedTick;
    uint32 elapsedUs = ticks / g_stmTicksPerUs;

    /* 100ms 주기 미달이면 스킵 */
    if (elapsedUs < (SPEED_CALC_INTERVAL_MS * 1000U)) return;

    /* uint32 언더플로우 방지: 현재 펄스가 이전보다 작으면 0 처리 */
    uint32 deltaPulse = 0U;
    if (g_avgPulse >= g_prevAvgPulse)
        deltaPulse = g_avgPulse - g_prevAvgPulse;

    /* elapsedUs 0 방지 */
    if (elapsedUs > 0U)
        g_vehicleSpeed = (deltaPulse * SPEED_CONV_FACTOR) / elapsedUs;
    else
        g_vehicleSpeed = 0U;

    g_prevAvgPulse  = g_avgPulse;
    g_prevSpeedTick = now;
}

void updateBrakeStateCan(void)
{
    if      (g_canCmdValid == 0U)                    g_brakeStateCan = BRAKE_CMD_HOLD;
    else if (g_canBrakeCmd == BRAKE_CMD_EMERGENCY)   g_brakeStateCan = BRAKE_CMD_EMERGENCY;
    else if (g_brakeApplied != 0U)                   g_brakeStateCan = BRAKE_CMD_HOLD;
    else                                             g_brakeStateCan = BRAKE_CMD_RELEASE;
}

void processBrakeCommand(void)
{
    g_gearMode = convertCanGearToMode(g_canGearState);

    if (g_canBrakeCmd == BRAKE_CMD_EMERGENCY)
    {
        g_brakeApplied = 1U; g_servoBrakeCmd = 1U;
    }
    else
    {
        g_brakeApplied = 0U; g_servoBrakeCmd = 0U;
    }
}

void enterWaitCommandStep(void)
{
    g_driveState = 9U; g_vehicleMoved = 0U;
    g_brakeApplied = 1U; g_servoBrakeCmd = 0U; g_nModeInitDone = 0U;

    IfxPort_setPinLow(M1_PWM_PORT, M1_PWM_PIN);
    IfxPort_setPinLow(M2_PWM_PORT, M2_PWM_PIN);
    motorsBrakeStop();
    servoWritePulseUs(SERVO_GTM_RELEASE);
}

void enterPModeStep(void)
{
    g_driveState = 0U; g_brakeApplied = 0U;
    g_servoBrakeCmd = 0U; g_vehicleMoved = 0U; g_nModeInitDone = 0U;

    /* GTM PWM 완전 정지 + 모든 핀 LOW (motor_ctrl 함수 사용) */
    motorsFullStop();
    servoWritePulseUs(SERVO_GTM_RELEASE);
}

void runRModeStep(void)
{
    g_nModeInitDone = 0U; g_directionForward = 0U;
    g_driveState = 2U; g_vehicleMoved = 0U;
    g_brakeApplied = 0U; g_servoBrakeCmd = 0U;

    motorsRunDuty(g_directionForward, DRIVE_M1_DUTY, DRIVE_M2_DUTY, DRIVE_CHUNK_MS);
}

void runDModeStep(void)
{
    g_nModeInitDone = 0U; g_directionForward = 1U;
    g_driveState = 4U; g_vehicleMoved = 0U;
    g_brakeApplied = 0U; g_servoBrakeCmd = 0U;

    motorsRunDuty(g_directionForward, DRIVE_M1_DUTY, DRIVE_M2_DUTY, DRIVE_CHUNK_MS);
}

void runNModeStep(void)
{
    IfxPort_setPinLow(M1_PWM_PORT, M1_PWM_PIN);
    IfxPort_setPinLow(M2_PWM_PORT, M2_PWM_PIN);
    g_vehicleMoved = 0U; g_brakeApplied = 0U;
    g_servoBrakeCmd = 0U; g_driveState = 5U;
    motorsReleaseBrake();
    servoWritePulseUs(SERVO_GTM_RELEASE);
}

void applyEmergencyBrakeStep(void)
{
    g_vehicleMoved = 1U; g_brakeApplied = 1U;
    g_servoBrakeCmd = 1U; g_driveState = 6U;
    motorsBrakeStop();
    servoHold(SERVO_GTM_BRAKE, 500U);   /* 500ms 동안 서보 제동 펄스 반복 */
}
