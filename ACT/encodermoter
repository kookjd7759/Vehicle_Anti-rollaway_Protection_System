#include "Ifx_Types.h"
#include "IfxCpu.h"
#include "IfxScuWdt.h"
#include "IfxPort.h"
#include "IfxStm.h"
#include "Bsp.h"

IFX_ALIGN(4) IfxCpu_syncEvent g_cpuSyncEvent = 0;

/* =========================================================
   Arduino Motor Shield Rev3 on TC375 Lite Kit
   ---------------------------------------------------------
   Motor A:
     PWM   = D3  -> P21.7
     BRAKE = D9  -> P02.7
     DIR   = D12 -> P10.1

   Motor B:
     PWM   = D11 -> P10.3
     BRAKE = D8  -> P02.6
     DIR   = D13 -> P10.2

   Encoder:
     Motor1 A = D4 -> P10.4
     Motor1 B = D5 -> P02.3
     Motor2 A = D6 -> P02.5
     Motor2 B = D7 -> P02.4
   ========================================================= */

/* ---------------- Motor A ---------------- */
#define M1_PWM_PORT      &MODULE_P21
#define M1_PWM_PIN       7

#define M1_BRAKE_PORT    &MODULE_P02
#define M1_BRAKE_PIN     7

#define M1_DIR_PORT      &MODULE_P10
#define M1_DIR_PIN       1

/* ---------------- Motor B ---------------- */
#define M2_PWM_PORT      &MODULE_P10
#define M2_PWM_PIN       3

#define M2_BRAKE_PORT    &MODULE_P02
#define M2_BRAKE_PIN     6

#define M2_DIR_PORT      &MODULE_P10
#define M2_DIR_PIN       2

/* ---------------- Encoder Motor 1 ---------------- */
#define ENC1_A_PORT      &MODULE_P10   /* D4 */
#define ENC1_A_PIN       4

#define ENC1_B_PORT      &MODULE_P02   /* D5 */
#define ENC1_B_PIN       3

/* ---------------- Encoder Motor 2 ---------------- */
#define ENC2_A_PORT      &MODULE_P02   /* D6 */
#define ENC2_A_PIN       5

#define ENC2_B_PORT      &MODULE_P02   /* D7 */
#define ENC2_B_PIN       4

/* ---------------- Low-speed software PWM ----------------
   너무 약하면 안 돌 수 있으니 2/18부터 시작 */
#define PWM_ON_MS        2
#define PWM_OFF_MS       18

#define PHASE_TIME_MS    2000

/* ---------------- Debug / Expression variables ---------------- */
volatile uint32 g_m1Pulse = 0;          /* Motor1 A상 상승엣지 수 */
volatile uint32 g_m1Change = 0;         /* Motor1 A상 변화 수 */
volatile uint8  g_m1AState = 0;
volatile uint8  g_m1PrevAState = 0;
volatile uint8  g_m1BState = 0;
volatile sint32 g_m1Position = 0;       /* 방향 포함 누적값 */
volatile sint8  g_m1Direction = 0;      /* 1 / -1 */

volatile uint32 g_m2Pulse = 0;          /* Motor2 A상 상승엣지 수 */
volatile uint32 g_m2Change = 0;         /* Motor2 A상 변화 수 */
volatile uint8  g_m2AState = 0;
volatile uint8  g_m2PrevAState = 0;
volatile uint8  g_m2BState = 0;
volatile sint32 g_m2Position = 0;       /* 방향 포함 누적값 */
volatile sint8  g_m2Direction = 0;      /* 1 / -1 */

volatile uint32 g_loopCount = 0;

/* ---------------- Function declarations ---------------- */
static void initMotorPins(void);
static void initEncoderPins(void);

static void motorsStop(void);
static void motorsRunSlow(uint8 forward, uint32 durationMs);

static uint8 readEnc1A(void);
static uint8 readEnc1B(void);
static uint8 readEnc2A(void);
static uint8 readEnc2B(void);

static void updateEncoders(void);

void core0_main(void)
{
    IfxCpu_enableInterrupts();

    IfxScuWdt_disableCpuWatchdog(IfxScuWdt_getCpuWatchdogPassword());
    IfxScuWdt_disableSafetyWatchdog(IfxScuWdt_getSafetyWatchdogPassword());

    IfxCpu_emitEvent(&g_cpuSyncEvent);
    IfxCpu_waitEvent(&g_cpuSyncEvent, 1);

    initMotorPins();
    initEncoderPins();

    /* 초기 정지 */
    motorsStop();
    waitTime(IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, 1000));

    /* 초기 엔코더 상태 저장 */
    g_m1PrevAState = readEnc1A();
    g_m1AState     = g_m1PrevAState;
    g_m1BState     = readEnc1B();

    g_m2PrevAState = readEnc2A();
    g_m2AState     = g_m2PrevAState;
    g_m2BState     = readEnc2B();

    while (1)
    {
        /* 2초 정방향 */
        motorsRunSlow(1, PHASE_TIME_MS);

        /* 2초 정지 */
        motorsStop();
        waitTime(IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, PHASE_TIME_MS));

        /* 2초 역방향 */
        motorsRunSlow(0, PHASE_TIME_MS);

        /* 2초 정지 */
        motorsStop();
        waitTime(IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, PHASE_TIME_MS));

        g_loopCount++;
    }
}

static void initMotorPins(void)
{
    /* Motor A */
    IfxPort_setPinModeOutput(M1_PWM_PORT,   M1_PWM_PIN,   IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinModeOutput(M1_BRAKE_PORT, M1_BRAKE_PIN, IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinModeOutput(M1_DIR_PORT,   M1_DIR_PIN,   IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);

    /* Motor B */
    IfxPort_setPinModeOutput(M2_PWM_PORT,   M2_PWM_PIN,   IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinModeOutput(M2_BRAKE_PORT, M2_BRAKE_PIN, IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);
    IfxPort_setPinModeOutput(M2_DIR_PORT,   M2_DIR_PIN,   IfxPort_OutputMode_pushPull, IfxPort_OutputIdx_general);

    /* 초기 정지 상태 */
    IfxPort_setPinLow(M1_PWM_PORT, M1_PWM_PIN);
    IfxPort_setPinHigh(M1_BRAKE_PORT, M1_BRAKE_PIN);
    IfxPort_setPinLow(M1_DIR_PORT, M1_DIR_PIN);

    IfxPort_setPinLow(M2_PWM_PORT, M2_PWM_PIN);
    IfxPort_setPinHigh(M2_BRAKE_PORT, M2_BRAKE_PIN);
    IfxPort_setPinLow(M2_DIR_PORT, M2_DIR_PIN);
}

static void initEncoderPins(void)
{
    IfxPort_setPinModeInput(ENC1_A_PORT, ENC1_A_PIN, IfxPort_InputMode_pullUp);
    IfxPort_setPinModeInput(ENC1_B_PORT, ENC1_B_PIN, IfxPort_InputMode_pullUp);

    IfxPort_setPinModeInput(ENC2_A_PORT, ENC2_A_PIN, IfxPort_InputMode_pullUp);
    IfxPort_setPinModeInput(ENC2_B_PORT, ENC2_B_PIN, IfxPort_InputMode_pullUp);
}

static void motorsStop(void)
{
    /* PWM OFF + Brake ON */
    IfxPort_setPinLow(M1_PWM_PORT, M1_PWM_PIN);
    IfxPort_setPinHigh(M1_BRAKE_PORT, M1_BRAKE_PIN);

    IfxPort_setPinLow(M2_PWM_PORT, M2_PWM_PIN);
    IfxPort_setPinHigh(M2_BRAKE_PORT, M2_BRAKE_PIN);
}

static void motorsRunSlow(uint8 forward, uint32 durationMs)
{
    uint32 elapsed = 0;

    /* 방향 설정 */
    if (forward)
    {
        IfxPort_setPinHigh(M1_DIR_PORT, M1_DIR_PIN);
        IfxPort_setPinHigh(M2_DIR_PORT, M2_DIR_PIN);
    }
    else
    {
        IfxPort_setPinLow(M1_DIR_PORT, M1_DIR_PIN);
        IfxPort_setPinLow(M2_DIR_PORT, M2_DIR_PIN);
    }

    /* 브레이크 해제 */
    IfxPort_setPinLow(M1_BRAKE_PORT, M1_BRAKE_PIN);
    IfxPort_setPinLow(M2_BRAKE_PORT, M2_BRAKE_PIN);

    while (elapsed < durationMs)
    {
        /* PWM ON */
        IfxPort_setPinHigh(M1_PWM_PORT, M1_PWM_PIN);
        IfxPort_setPinHigh(M2_PWM_PORT, M2_PWM_PIN);

        updateEncoders();
        waitTime(IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, PWM_ON_MS));
        updateEncoders();

        /* PWM OFF */
        IfxPort_setPinLow(M1_PWM_PORT, M1_PWM_PIN);
        IfxPort_setPinLow(M2_PWM_PORT, M2_PWM_PIN);

        updateEncoders();
        waitTime(IfxStm_getTicksFromMilliseconds(BSP_DEFAULT_TIMER, PWM_OFF_MS));
        updateEncoders();

        elapsed += (PWM_ON_MS + PWM_OFF_MS);
    }
}

static uint8 readEnc1A(void)
{
    return IfxPort_getPinState(ENC1_A_PORT, ENC1_A_PIN) ? 1 : 0;
}

static uint8 readEnc1B(void)
{
    return IfxPort_getPinState(ENC1_B_PORT, ENC1_B_PIN) ? 1 : 0;
}

static uint8 readEnc2A(void)
{
    return IfxPort_getPinState(ENC2_A_PORT, ENC2_A_PIN) ? 1 : 0;
}

static uint8 readEnc2B(void)
{
    return IfxPort_getPinState(ENC2_B_PORT, ENC2_B_PIN) ? 1 : 0;
}

static void updateEncoders(void)
{
    /* -------- Motor 1 -------- */
    g_m1AState = readEnc1A();
    g_m1BState = readEnc1B();

    if (g_m1AState != g_m1PrevAState)
    {
        g_m1Change++;

        if ((g_m1PrevAState == 0) && (g_m1AState == 1))
        {
            g_m1Pulse++;

            if (g_m1BState == 0)
            {
                g_m1Direction = 1;
                g_m1Position++;
            }
            else
            {
                g_m1Direction = -1;
                g_m1Position--;
            }
        }

        g_m1PrevAState = g_m1AState;
    }

    /* -------- Motor 2 -------- */
    g_m2AState = readEnc2A();
    g_m2BState = readEnc2B();

    if (g_m2AState != g_m2PrevAState)
    {
        g_m2Change++;

        if ((g_m2PrevAState == 0) && (g_m2AState == 1))
        {
            g_m2Pulse++;

            if (g_m2BState == 0)
            {
                g_m2Direction = 1;
                g_m2Position++;
            }
            else
            {
                g_m2Direction = -1;
                g_m2Position--;
            }
        }

        g_m2PrevAState = g_m2AState;
    }
}
