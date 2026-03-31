/**********************************************************************************************************************
 * \file ultrasonic_isr.c
 * \brief HC-SR04 초음파 센서 - ERU 인터럽트 기반 비블로킹 측정
 *
 * Echo 핀(P02.1)의 Rising/Falling Edge를 ERU 인터럽트로 감지하여
 * STM 타임스탬프를 캡처합니다. CPU 점유 시간: Trigger 12us + ISR 수us
 *
 * ERU 매핑 (IfxScu_PinMap.h 확인):
 *   P02.1 = REQ2B
 *   -> Input Channel 2, EXIS = 1 (B)
 *   -> Output Channel 2
 *   -> SRC_SCUERU2 (주소 0xF0038888, IfxSrc_reg.h 확인)
 *
 * IGCR[1] 비트 레이아웃 (IfxScu_regdef.h 확인):
 *   [2]     IPEN02 - Input Channel 2 패턴 활성화
 *   [13]    GEEN0  - Generate Event Enable
 *   [15:14] IGP0   - Interrupt Gating Pattern
 *********************************************************************************************************************/
#include "App/ultrasonic_isr.h"
#include "IfxPort.h"
#include "IfxStm_reg.h"
#include "IfxScu_reg.h"
#include "IfxScuWdt.h"

#define STM_FREQ        100000000u   /* STM = 100MHz */
#define ERU_ISR_PRIO    40

/* SRC_SCUERU2: IfxSrc_reg.h에서 확인한 주소 */
#define SRC_SCUERU2_ADDR  0xF0038888u

/* ── 전역 변수 ── */
UltraResult g_ultra = { ULTRA_IDLE, 0, 0, 0 };
volatile uint32 g_eru_isr_count = 0;

/* ── STM 타이머 ── */
static uint32 timer_now(void)
{
    return MODULE_STM0.TIM0.U;
}

static void delay_us_local(uint32 us)
{
    uint32 start = timer_now();
    uint32 ticks = us * (STM_FREQ / 1000000u);
    while ((timer_now() - start) < ticks);
}

/* ══════════════════════════════════════
 *  ERU ISR: Echo 핀 엣지 감지
 * ══════════════════════════════════════ */
IFX_INTERRUPT(eru_ultrasonic_isr, 0, ERU_ISR_PRIO);

void eru_ultrasonic_isr(void)
{
    uint32 now = timer_now();
    g_eru_isr_count++;

    if (IfxPort_getPinState(&MODULE_P02, 1))
    {
        /* Rising Edge: Echo HIGH 시작 */
        g_ultra.rise_tick = now;
        g_ultra.state = ULTRA_MEASURING;
    }
    else
    {
        /* Falling Edge: Echo HIGH 끝 */
        g_ultra.fall_tick = now;
        g_ultra.echo_us = (g_ultra.fall_tick - g_ultra.rise_tick)
                          / (STM_FREQ / 1000000u);
        g_ultra.state = ULTRA_DONE;
    }

    /* ERU 플래그 클리어 (FMR.FC2 = bit 18) */
    MODULE_SCU.FMR.U = (1u << 18);
}

/* ══════════════════════════════════════
 *  ERU 초기화
 * ══════════════════════════════════════ */
void Ultrasonic_Init(void)
{
    /* Trig 핀 출력 */
    IfxPort_setPinModeOutput(&MODULE_P02, 0,
        IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);

    /* Echo 핀 입력 */
    IfxPort_setPinModeInput(&MODULE_P02, 1, IfxPort_InputMode_noPullDevice);

    /* ── 1. SRC 인터럽트 비활성화 (CPU ENDINIT) ── */
    {
        uint16 cpuPw = IfxScuWdt_getCpuWatchdogPassword();
        IfxScuWdt_clearCpuEndinit(cpuPw);

        volatile uint32 *srcReg = (volatile uint32 *)SRC_SCUERU2_ADDR;
        *srcReg = (1u << 25);  /* CLRR=1, SRE=0 */

        IfxScuWdt_setCpuEndinit(cpuPw);
    }

    /* ── 2. ERU 설정 (Safety ENDINIT 해제 필요) ── */
    {
        uint16 safetyPw = IfxScuWdt_getSafetyWatchdogPassword();
        IfxScuWdt_clearSafetyEndinit(safetyPw);

        /* ERU Input Channel 2: EICR[1] 하위 16비트
         *   [6:4]  EXIS0 = 1 (REQ2B = P02.1)
         *   [8]    FEN0  = 1 (Falling Edge)
         *   [9]    REN0  = 1 (Rising Edge)
         */
        {
            uint32 eicr = MODULE_SCU.EICR[1].U;
            eicr &= ~((0x7u << 4) | (1u << 8) | (1u << 9) | (1u << 11) | (0x7u << 12));
            eicr |= (1u << 4);     /* EXIS0 = 1: REQ2B = P02.1 */
            eicr |= (1u << 8);     /* FEN0 = 1 */
            eicr |= (1u << 9);     /* REN0 = 1 */
            eicr |= (1u << 11);    /* EIEN0 = 1: 외부 입력 활성화 */
            eicr |= (2u << 12);    /* INP0 = 2: Output Channel 2로 라우팅 */
            MODULE_SCU.EICR[1].U = eicr;
        }

        /* ERU Output Channel 2: IGCR[1] 하위 16비트
         *   [2]     IPEN02 = 1 (Input Channel 2 패턴 활성화)
         *   [15:14] IGP0   = 01 (패턴 매치 시 인터럽트 트리거)
         */
        {
            uint32 igcr = MODULE_SCU.IGCR[1].U;
            igcr &= ~((0x3u << 14) | (1u << 2));  /* IGP0 + IPEN02 클리어 */
            igcr |= (1u << 2);      /* IPEN02 = 1 */
            igcr |= (0x1u << 14);   /* IGP0 = 01 */
            MODULE_SCU.IGCR[1].U = igcr;
        }

        /* 플래그 클리어 */
        MODULE_SCU.FMR.U = (1u << 18);

        IfxScuWdt_setSafetyEndinit(safetyPw);
    }

    /* ── 3. SRC 인터럽트 활성화 (CPU ENDINIT) ── */
    {
        uint16 cpuPw = IfxScuWdt_getCpuWatchdogPassword();
        IfxScuWdt_clearCpuEndinit(cpuPw);

        volatile uint32 *srcReg = (volatile uint32 *)SRC_SCUERU2_ADDR;
        *srcReg = (1u << 25)                       /* CLRR = 1 */
                | ((uint32)ERU_ISR_PRIO << 0)      /* SRPN = 40 */
                | (0u << 11)                        /* TOS = CPU0 */
                | (1u << 10);                       /* SRE = 1 */

        IfxScuWdt_setCpuEndinit(cpuPw);
    }
}

/* ── Trig 펄스 (비블로킹, ~12us) ── */
void Ultrasonic_Trigger(void)
{
    g_ultra.state   = ULTRA_WAIT_ECHO;
    g_ultra.echo_us = 0;

    IfxPort_setPinLow(&MODULE_P02, 0);
    delay_us_local(2);
    IfxPort_setPinHigh(&MODULE_P02, 0);
    delay_us_local(10);
    IfxPort_setPinLow(&MODULE_P02, 0);
}

boolean Ultrasonic_IsDone(void)
{
    return (g_ultra.state == ULTRA_DONE);
}

uint32 Ultrasonic_GetEchoUs(void)
{
    if (g_ultra.state == ULTRA_DONE)
        return g_ultra.echo_us;
    return 0;
}
