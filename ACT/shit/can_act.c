#ifndef CAN_ACT_H
#define CAN_ACT_H

#include "Ifx_Types.h"
#include "IfxCan_Can.h"

/* ── CAN 핀 / ID / 타임아웃 ── */
#define CAN_STB_PORT    &MODULE_P20
#define CAN_STB_PIN     6

#define CAN_RX_ID       0x100U
#define CAN_TX_ID       0x300U
#define CAN_TIMEOUT_US  500000U

/*
 * ── MAIN → ACT 수신 프레임 (ID: 0x100, 2바이트) ──
 * Byte 0 : brake_cmd   0:해제 / 1:유지 / 2:긴급제동
 * Byte 1 : gear_state  0:P / 1:R / 2:N / 3:D
 *
 * ── ACT → MAIN 송신 프레임 (ID: 0x300, 8바이트) ──
 * Byte 0~3 : speed       (uint32, km/h × 100, little-endian)
 * Byte 4   : brake_state (0:해제 / 1:유지 / 2:긴급)
 * Byte 5   : accel_x     (sint8, g × 100, ±100)
 * Byte 6   : accel_y     (sint8, g × 100, ±100)
 * Byte 7   : accel_z     (sint8, g × 100, ±100)
 */

/* ── Brake / Gear 열거형 ── */
typedef enum { BRAKE_CMD_RELEASE=0, BRAKE_CMD_HOLD=1, BRAKE_CMD_EMERGENCY=2 } BrakeCmd;
typedef enum { CAN_GEAR_P=0, CAN_GEAR_R=1, CAN_GEAR_N=2, CAN_GEAR_D=3 }      CanGearState;

/* ── CAN 수신 데이터 ── */
extern volatile uint8  g_canBrakeCmd;
extern volatile uint8  g_canGearState;
extern volatile uint8  g_canCmdValid;
extern volatile uint32 g_lastRxTick;

/* ── CAN 송신 데이터 ── */
extern volatile uint32 g_vehicleSpeed;
extern volatile uint8  g_brakeStateCan;

/* ── 디버그 ── */
extern volatile uint8  g_txSuccess, g_txBusy, g_txFail;
extern volatile uint32 g_txCount;
extern volatile uint8  g_rxNew, g_rxFail;
extern volatile uint32 g_rxCount;
extern volatile uint32 g_rxData0, g_rxData1;
extern volatile uint8  g_canBusOffCount;   /* Bus-Off 발생 횟수 디버그용 */

/* ── CAN 모듈 객체 ── */
extern IfxCan_Can      g_mcmcan;
extern IfxCan_Can_Node g_canNode0;

/* ── 공개 함수 ── */
void initCanAct(void);
void receive_main_command(void);
void checkCanTimeout(void);
void checkCanBusOff(void);     /* Bus-Off 감지 및 자동 복구 */
void send_act_status(void);

#endif /* CAN_ACT_H */
