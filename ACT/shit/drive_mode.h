#ifndef DRIVE_MODE_H
#define DRIVE_MODE_H

#include "Ifx_Types.h"
#include "can_act.h"

/* ── 속도 계산 파라미터 ── */
#define SPEED_CALC_INTERVAL_MS  100U
#define SPEED_CONV_FACTOR       243750U

/* ── GearMode ── */
typedef enum { GEAR_P=0, GEAR_R=1, GEAR_D=2, GEAR_N=3 } GearMode;

/* ── 상태 전역 ── */
extern volatile GearMode g_gearMode;
extern volatile uint8    g_driveState;
extern volatile uint8    g_brakeApplied;
extern volatile uint8    g_vehicleMoved;
extern volatile uint32   g_stmTicksPerUs;  /* can_act.c의 checkCanTimeout도 사용 */

/* ── 공개 함수 ── */
GearMode convertCanGearToMode(uint8 canGear);
void updateVehicleSpeed(void);
void updateBrakeStateCan(void);
void processBrakeCommand(void);

void enterWaitCommandStep(void);
void enterPModeStep(void);
void runRModeStep(void);
void runDModeStep(void);
void runNModeStep(void);
void applyEmergencyBrakeStep(void);

#endif /* DRIVE_MODE_H */
