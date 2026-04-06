#ifndef MOTOR_CTRL_H
#define MOTOR_CTRL_H

#include "Ifx_Types.h"

/* ── 핀 정의 (TC375 Lite Kit V2 + Arduino Motor Shield Rev3)
 *
 *  ⚠️ 핀 충돌 해결 방법:
 *     모터쉴드 D8(BRAKE_B), D9(BRAKE_A) 점퍼를 GND에 단락
 *     → 브레이크 항상 해제 고정
 *     → 소프트웨어에서 PWM=0 으로 정지 처리
 *
 *  Ch.A (오른쪽 M2): D3=PWM / D12=DIR
 *  Ch.B (왼쪽  M1): D11=PWM / D13=DIR
 *
 *  D3  → P21.7
 *  D11 → P02.7  (BRAKE_A D9도 P02.7이므로 BRAKE 점퍼 GND 단락 필수!)
 *  D12 → P15.4
 *  D13 → P15.5
 */

/* ── M1 (왼쪽, Channel B) ── */
#define M1_PWM_PORT   &MODULE_P10
#define M1_PWM_PIN    3          /* Arduino D11 → P02.7 */
#define M1_BRAKE_PORT &MODULE_P02
#define M1_BRAKE_PIN  6          /* Arduino D8  → P02.6 (점퍼 GND 단락) */
#define M1_DIR_PORT   &MODULE_P10
#define M1_DIR_PIN    2          /* Arduino D13 → P15.5 */

/* ── M2 (오른쪽, Channel A) ── */
#define M2_PWM_PORT   &MODULE_P02
#define M2_PWM_PIN    1          /* Arduino D3  → P21.7 */
#define M2_BRAKE_PORT &MODULE_P02
#define M2_BRAKE_PIN  7          /* Arduino D9  → P02.7 (점퍼 GND 단락) */
#define M2_DIR_PORT   &MODULE_P10
#define M2_DIR_PIN    1          /* Arduino D12 → P15.4 */

/* ── 엔코더 핀 ── */
#define ENC1_A_PORT   &MODULE_P10
#define ENC1_A_PIN    4
#define ENC1_B_PORT   &MODULE_P02
#define ENC1_B_PIN    3

#define ENC2_A_PORT   &MODULE_P02
#define ENC2_A_PIN    5
#define ENC2_B_PORT   &MODULE_P02
#define ENC2_B_PIN    4

/* ── 서보 핀 ── */
#define SERVO_PORT    &MODULE_P10
#define SERVO_PIN     5

/* ── PWM / 서보 파라미터 ── */
#define SERVO_GTM_PERIOD  7813U
#define SERVO_GTM_RELEASE 700U
#define SERVO_GTM_BRAKE   500U

#define SERVO_RELEASE_US  1500U
#define SERVO_BRAKE_US    1800U
#define PWM_PERIOD_US     1000U
#define DRIVE_CHUNK_MS    20U
#define DRIVE_M1_DUTY     70U    /* 3% → 20% 로 상향 */
#define DRIVE_M2_DUTY     70U

/* ── 엔코더 전역 ── */
extern volatile uint32 g_m1Pulse, g_m2Pulse, g_avgPulse;
extern volatile sint32 g_m1Position, g_m2Position;
extern volatile sint8  g_m1Direction, g_m2Direction;

/* ── 서보 상태 ── */
extern volatile uint16 g_servoPulseUs;

/* ── 공개 함수 ── */
void initMotorPins(void);
void initEncoderPins(void);
void initServoPin(void);

void motorsFullStop(void);   /* GTM PWM 정지 + 모든 핀 LOW (P단 전용) */
void motorsBrakeStop(void);
void motorsReleaseBrake(void);
void motorsRunDuty(uint8 forward, uint8 dutyM1, uint8 dutyM2, uint32 runTimeMs);

void updateEncoders(void);

void servoWritePulseUs(uint16 pulseUs);
void servoHold(uint16 pulseUs, uint32 durationMs);
void delayUs(uint32 us);
void init_pwm(void);

#endif /* MOTOR_CTRL_H */
