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

/* ── Brake / Gear 열거형 ── */
typedef enum { BRAKE_CMD_RELEASE=0, BRAKE_CMD_HOLD=1, BRAKE_CMD_EMERGENCY=2 } BrakeCmd;
typedef enum { CAN_GEAR_P=0, CAN_GEAR_R=1, CAN_GEAR_N=2, CAN_GEAR_D=3 }      CanGearState;

/* ── CAN 수신 데이터 (외부 공유) ── */
extern volatile uint8  g_canBrakeCmd;
extern volatile uint8  g_canGearState;
extern volatile uint8  g_canCmdValid;
extern volatile uint32 g_lastRxTick;

/* ── CAN 송신 데이터 (drive_mode에서 채움) ── */
extern volatile uint32 g_vehicleSpeed;
extern volatile uint8  g_brakeStateCan;

/* ── CAN 디버그 ── */
extern volatile uint8  g_txSuccess, g_txBusy, g_txFail;
extern volatile uint32 g_txCount;
extern volatile uint8  g_rxNew,  g_rxFail;
extern volatile uint32 g_rxCount;
extern volatile uint32 g_rxData0, g_rxData1;

/* ── CAN 모듈 객체 (motor_ctrl 등에서 직접 접근 불필요) ── */
extern IfxCan_Can      g_mcmcan;
extern IfxCan_Can_Node g_canNode0;

/* ── 공개 함수 ── */
void initCanAct(void);
void receive_main_command(void);
void checkCanTimeout(void);
void send_act_status(void);

#endif /* CAN_ACT_H */
