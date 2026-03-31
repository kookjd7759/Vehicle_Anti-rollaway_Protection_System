/**********************************************************************************************************************
 * \file ultrasonic_isr.h
 * \brief 초음파 HC-SR04 - ERU 인터럽트 기반 비블로킹 측정
 * \pin   Trig=P02.0(D2), Echo=P02.1(D3)
 *********************************************************************************************************************/
#ifndef APP_ULTRASONIC_ISR_H_
#define APP_ULTRASONIC_ISR_H_

#include "Ifx_Types.h"

/* 측정 상태 */
typedef enum {
    ULTRA_IDLE,         /* 대기 중 */
    ULTRA_WAIT_ECHO,    /* Trig 전송 후 Echo 대기 */
    ULTRA_MEASURING,    /* Echo HIGH 구간 측정 중 */
    ULTRA_DONE,         /* 측정 완료, echo_us 유효 */
    ULTRA_TIMEOUT       /* 타임아웃 */
} UltraState;

/* 측정 결과 (ISR에서 갱신) */
typedef struct {
    volatile UltraState state;
    volatile uint32     rise_tick;
    volatile uint32     fall_tick;
    volatile uint32     echo_us;
} UltraResult;

extern UltraResult g_ultra;

/* 초기화: ERU 인터럽트 + Trig 핀 설정 */
void Ultrasonic_Init(void);

/* Trig 펄스 전송 (비블로킹, ~12us) */
void Ultrasonic_Trigger(void);

/* 측정 완료 여부 */
boolean Ultrasonic_IsDone(void);

/* 최근 echo 시간 읽기 (us, 0=미완료) */
uint32 Ultrasonic_GetEchoUs(void);

#endif
