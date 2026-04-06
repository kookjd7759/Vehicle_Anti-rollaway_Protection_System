/**********************************************************************************************************************
 * \file sensor_driver.c
 * \brief VAPS MAIN ECU - 센서 드라이버 (ERU 인터럽트 기반 초음파)
 *
 * Task_Sensor (10ms 주기):
 *   - 기어/도어 GPIO 읽기 + 디바운싱       ~수 us
 *   - 초음파: 결과 확인 + Trigger (비블로킹) ~12 us
 *   - 센서 단위 변환 + 3샘플 중앙값 필터
 *   - 운전자 퓨전 판정(점수/가중치 기반)
 *   - LED 제어
 *   - 총 실행 시간: ~1ms 이내 (10ms 주기 정확히 유지)
 *********************************************************************************************************************/
#include "App/sensor_data.h"
#include "App/tof_sensor.h"
#include "App/ultrasonic_isr.h"
#include "FreeRTOS.h"
#include "task.h"
#include "Evadc/Adc/IfxEvadc_Adc.h"
#include "IfxPort.h"
#include "IfxStm_reg.h"

/* Cpu0_Main.c에 정의된 LED 함수 */
extern void Debug_LED_Set(uint8 on);
extern void Board_LED1_Set(uint8 on);
extern void Board_LED2_Set(uint8 on);

/* ══════════════════════════════════════
 *  센서 융합 설정
 * ══════════════════════════════════════ */
#define RAW_FILTER_SIZE      3u
#define PRESSURE_SENSOR_COUNT 3u
#define MIN_DIST_MM          20
#define MAX_DIST_MM          4000
#define STM_FREQ             100000000u
#define ULTRA_TIMEOUT_TICKS  5000000u  /* 50ms */
#define PRESSURE_MAX_ADC     ((uint32)4095U)
#define PRESSURE_MAX_G       ((uint32)10000U)
#define PRESENT_CONFIRM_CNT  ((uint8)3U)
#define ABSENT_CONFIRM_CNT   ((uint8)3U)
#define WS_ABSENT_TH_X10     ((uint16)10U)
#define PRESSURE_ADC_GROUP_ID IfxEvadc_GroupId_8 /* TC37A pin map: P40.6/7/8 -> EVADC G8 CH4/5/6 */

/* 초음파 비블로킹 상태 머신 */
typedef enum {
    US_TRIGGER,     /* Trig 전송 */
    US_WAIT,        /* ISR 완료 대기 */
    US_COOLDOWN     /* 다음 측정 전 대기 (에코 잔향 소멸) */
} UltraSensorPhase;

static UltraSensorPhase ultra_phase = US_TRIGGER;
static uint32 ultra_trigger_tick = 0;
static int latest_distance_mm = 0;

/* ══════════════════════════════════════
 *  디바운싱 버퍼
 * ══════════════════════════════════════ */
/* 기어: 개별 버튼 디바운스 + press-release 래치 */
static boolean gear_p_buf[3] = {0, 0, 0};
static boolean gear_r_buf[3] = {0, 0, 0};
static boolean gear_n_buf[3] = {0, 0, 0};
static boolean gear_d_buf[3] = {0, 0, 0};
static boolean gear_p_stable = FALSE;
static boolean gear_r_stable = FALSE;
static boolean gear_n_stable = FALSE;
static boolean gear_d_stable = FALSE;
static boolean gear_p_armed = FALSE;
static boolean gear_r_armed = FALSE;
static boolean gear_n_armed = FALSE;
static boolean gear_d_armed = FALSE;

static boolean door_sw_buf[3] = {0, 0, 0};
static uint8 db_idx = 0;
static uint16 ultra_buf[RAW_FILTER_SIZE] = {0, 0, 0};
static uint16 tof_buf[RAW_FILTER_SIZE]   = {0, 0, 0};
static uint32 press_buf[RAW_FILTER_SIZE] = {0, 0, 0};
static uint8 sensor_filt_idx = 0;
static uint8 present_count = 0;
static uint8 absent_count = 0;
static DriverState stable_driver_state = DRIVER_ABSENT;

/* ══════════════════════════════════════
 *  디버깅용 전역 변수
 * ══════════════════════════════════════ */
volatile int        g_ultra_mm       = 0;
volatile uint16     g_pressure_adc   = 0; /* 3 FSR average ADC */
volatile uint16     g_pressure_raw   = 0; /* 3 FSR average ADC */
volatile uint16     g_pressure_adc_1 = 0;
volatile uint16     g_pressure_adc_2 = 0;
volatile uint16     g_pressure_adc_3 = 0;
volatile uint16     g_tof_mm         = 0;
volatile GearState  g_gear_debug     = GEAR_P;
volatile DoorState  g_door_debug     = DOOR_CLOSE;
volatile DriverState g_driver_debug  = DRIVER_ABSENT;
volatile boolean    g_pin_gear_p     = 0;
volatile boolean    g_pin_gear_r     = 0;
volatile boolean    g_pin_gear_n     = 0;
volatile boolean    g_pin_gear_d     = 0;
volatile int        g_gear_count     = 0;
volatile boolean    g_raw_p02_3      = 0;
volatile boolean    g_raw_p02_5      = 0;
volatile boolean    g_raw_p02_4      = 0;
volatile boolean    g_raw_p02_6      = 0;
volatile boolean    g_pressure_present_debug = FALSE;
volatile uint8      g_score_ultra    = 0;
volatile uint8      g_score_tof      = 0;
volatile uint8      g_score_press    = 0;
volatile uint16     g_ws_x10         = 0;
volatile uint16     g_ultra_cm       = 0;
volatile uint16     g_tof_cm         = 0;
volatile uint32     g_press_g        = 0;
volatile uint32     g_press_avg_g    = 0;


/* ══════════════════════════════════════
 *  STM 타이머 (타임아웃 확인용)
 * ══════════════════════════════════════ */
static uint32 timer_now(void)
{
    return MODULE_STM0.TIM0.U;
}

/* ══════════════════════════════════════
 *  압력 센서 EVADC
 * ══════════════════════════════════════ */
static IfxEvadc_Adc g_evadc;
static IfxEvadc_Adc_Group g_adc_group;
static IfxEvadc_Adc_Channel g_adc_channels[PRESSURE_SENSOR_COUNT];
static uint16 g_pressure_last_raw[PRESSURE_SENSOR_COUNT] = {0U, 0U, 0U};
static const IfxEvadc_ChannelId g_pressure_channel_ids[PRESSURE_SENSOR_COUNT] = {
    IfxEvadc_ChannelId_4,
    IfxEvadc_ChannelId_5,
    IfxEvadc_ChannelId_6
};
static const IfxEvadc_ChannelResult g_pressure_result_regs[PRESSURE_SENSOR_COUNT] = {
    IfxEvadc_ChannelResult_4,
    IfxEvadc_ChannelResult_5,
    IfxEvadc_ChannelResult_6
};

static void init_pressure_sensor(void)
{
    IfxEvadc_Adc_Config adc_config;
    IfxEvadc_Adc_GroupConfig group_config;
    IfxEvadc_Adc_ChannelConfig channel_config;
    uint32 index;

    IfxEvadc_Adc_initModuleConfig(&adc_config, &MODULE_EVADC);
    IfxEvadc_Adc_initModule(&g_evadc, &adc_config);

    IfxEvadc_Adc_initGroupConfig(&group_config, &g_evadc);
    group_config.groupId = PRESSURE_ADC_GROUP_ID;
    group_config.master = PRESSURE_ADC_GROUP_ID;
    group_config.arbiter.requestSlotQueue0Enabled = TRUE;
    group_config.queueRequest[0].triggerConfig.gatingMode = IfxEvadc_GatingMode_always;
    IfxEvadc_Adc_initGroup(&g_adc_group, &group_config);

    IfxPort_setPinModeInput(&MODULE_P40, 6, IfxPort_InputMode_noPullDevice);
    IfxPort_setPinModeInput(&MODULE_P40, 7, IfxPort_InputMode_noPullDevice);
    IfxPort_setPinModeInput(&MODULE_P40, 8, IfxPort_InputMode_noPullDevice);

    for (index = 0U; index < PRESSURE_SENSOR_COUNT; index++)
    {
        IfxEvadc_Adc_initChannelConfig(&channel_config, &g_adc_group);
        channel_config.channelId = g_pressure_channel_ids[index];
        channel_config.resultRegister = g_pressure_result_regs[index];
        IfxEvadc_Adc_initChannel(&g_adc_channels[index], &channel_config);
        IfxEvadc_Adc_addToQueue(&g_adc_channels[index], IfxEvadc_RequestSource_queue0, IFXEVADC_QUEUE_REFILL);
    }

    IfxEvadc_Adc_startQueue(&g_adc_group, IfxEvadc_RequestSource_queue0);
}

static uint16 read_pressure_adc_average(void)
{
    uint32 sum = 0U;
    uint32 index;

    for (index = 0U; index < PRESSURE_SENSOR_COUNT; index++)
    {
        Ifx_EVADC_G_RES result = IfxEvadc_Adc_getResult(&g_adc_channels[index]);

        if (result.B.VF)
            g_pressure_last_raw[index] = (uint16)result.B.RESULT;

        sum += g_pressure_last_raw[index];
    }

    return (uint16)((sum + (PRESSURE_SENSOR_COUNT / 2U)) / PRESSURE_SENSOR_COUNT);
}



/* ══════════════════════════════════════
 *  기어 읽기 (디버그 변수 갱신용)
 * ══════════════════════════════════════ */
static void read_gear_raw(void)
{
    g_raw_p02_3 = IfxPort_getPinState(&MODULE_P02, 3);
    g_raw_p02_5 = IfxPort_getPinState(&MODULE_P02, 5);
    g_raw_p02_4 = IfxPort_getPinState(&MODULE_P02, 4);
    g_raw_p02_6 = IfxPort_getPinState(&MODULE_P02, 6);

    g_pin_gear_p = g_raw_p02_3;
    g_pin_gear_r = g_raw_p02_5;
    g_pin_gear_n = g_raw_p02_4;
    g_pin_gear_d = g_raw_p02_6;
    g_gear_count = g_raw_p02_3 + g_raw_p02_5 + g_raw_p02_4 + g_raw_p02_6;
}

/* ══════════════════════════════════════
 *  도어 읽기
 * ══════════════════════════════════════ */
static boolean read_door_switch_raw(void)
{
    return IfxPort_getPinState(&MODULE_P02, 7) ? TRUE : FALSE;
}

/* ══════════════════════════════════════
 *  센서 변환/필터/판정 함수
 * ══════════════════════════════════════ */
static int us_to_mm(uint32 echo_us)
{
    return (int)((echo_us * 10u + 29u) / 58u);
}

static uint16 ultra_mm_to_cm(int ultra_mm)
{
    if (ultra_mm <= 0)
        return 0;

    return (uint16)(((uint32)ultra_mm) / 10u);
}

static uint16 tof_mm_to_cm(uint16 tof_mm)
{
    return (uint16)(tof_mm / 10u);
}

static uint32 pressure_adc_to_g(uint16 pressure_adc)
{
    return ((uint32)pressure_adc * PRESSURE_MAX_G) / PRESSURE_MAX_ADC;
}

static uint16 median3(uint16 a, uint16 b, uint16 c)
{
    if (a > b) { uint16 t = a; a = b; b = t; }
    if (b > c) { uint16 t = b; b = c; c = t; }
    if (a > b) { uint16 t = a; a = b; b = t; }
    return b;
}

static uint32 median3_u32(uint32 a, uint32 b, uint32 c)
{
    if (a > b) { uint32 t = a; a = b; b = t; }
    if (b > c) { uint32 t = b; b = c; c = t; }
    if (a > b) { uint32 t = a; a = b; b = t; }
    return b;
}

static uint8 score_ultrasonic(uint16 cm)
{
    if (cm < 2u)   return 0u;   /* 노이즈/너무 가까움 */
    if (cm < 8u)   return 5u;   /* 착석: 5cm 부근 */
    if (cm < 12u)  return 3u;
    if (cm < 15u)  return 1u;
    return 0u;                  /* 부재: 19cm+ */
}

static uint8 score_tof(uint16 cm)
{
    if (cm < 5u)   return 0u;   /* 노이즈/너무 가까움 */
    if (cm < 13u)  return 5u;   /* 착석: 10cm 부근 */
    if (cm < 16u)  return 3u;
    if (cm < 18u)  return 1u;
    return 0u;                  /* 부재: 20-21cm+ */
}

static uint8 score_pressure(uint32 g)
{
    if (g < 200u)     return 0u;   /* 부재: ADC 0~80 → ~0~195g */
    if (g < 2000u)    return 1u;
    if (g < 4000u)    return 3u;
    return 5u;                     /* 착석: ADC 2000+ → ~4884g+ */
}

static uint16 calc_ws_x10(uint8 ru, uint8 rt, uint8 rp)
{
    return (uint16)((3u * ru) + (3u * rt) + (4u * rp));
}

static DriverState judge_raw(uint16 ws_x10)
{
    if (ws_x10 <= WS_ABSENT_TH_X10)
        return DRIVER_ABSENT;

    return DRIVER_SEATED;
}

/* ══════════════════════════════════════
 *  초음파 비블로킹 상태 머신
 *
 *  10ms마다 호출됨. 상태에 따라 동작:
 *  US_TRIGGER  → Trig 전송 (12us)
 *  US_WAIT     → ISR 완료 확인 (즉시 리턴)
 *  US_COOLDOWN → 에코 잔향 소멸 대기 (10ms)
 * ══════════════════════════════════════ */
static void ultrasonic_update(void)
{
    switch (ultra_phase)
    {
    case US_TRIGGER:
        Ultrasonic_Trigger();
        ultra_trigger_tick = timer_now();
        ultra_phase = US_WAIT;
        break;

    case US_WAIT:
        if (Ultrasonic_IsDone())
        {
            int distance_mm = us_to_mm(Ultrasonic_GetEchoUs());

            if ((distance_mm >= MIN_DIST_MM) && (distance_mm <= MAX_DIST_MM))
                latest_distance_mm = distance_mm;
            else
                latest_distance_mm = 0;

            ultra_trigger_tick = timer_now();
            ultra_phase = US_COOLDOWN;
        }
        else if ((timer_now() - ultra_trigger_tick) > ULTRA_TIMEOUT_TICKS)
        {
            latest_distance_mm = 0;
            ultra_trigger_tick = timer_now();
            ultra_phase = US_COOLDOWN;
        }
        break;

    case US_COOLDOWN:
        if ((timer_now() - ultra_trigger_tick) > (STM_FREQ / 100u))
            ultra_phase = US_TRIGGER;
        break;
    }
}

static DriverState judge_driver_final(uint8 ru, uint8 rt, uint8 rp)
{
    uint16 ws_x10 = calc_ws_x10(ru, rt, rp);
    DriverState raw = judge_raw(ws_x10);

    g_ws_x10 = ws_x10;

    if (raw == DRIVER_SEATED)
    {
        if (present_count < 255u)
            present_count++;
        absent_count = 0;
    }
    else
    {
        if (absent_count < 255u)
            absent_count++;
        present_count = 0;
    }

    if (present_count >= PRESENT_CONFIRM_CNT)
        stable_driver_state = DRIVER_SEATED;
    else if (absent_count >= ABSENT_CONFIRM_CNT)
        stable_driver_state = DRIVER_ABSENT;

    return stable_driver_state;
}

/* ══════════════════════════════════════
 *  Task_Sensor: 10ms 주기
 *
 *  실행 시간: ~1ms 이내 (비블로킹)
 *  10ms 주기 정확히 유지됨
 * ══════════════════════════════════════ */
void Task_Sensor(void *param)
{
    TickType_t xLastWake = xTaskGetTickCount();
    static GearState gear_ok = GEAR_P;
    static GearState last_physical_gear = GEAR_P;
    static DoorState door_ok = DOOR_OPEN;

    init_pressure_sensor();

    while (1)
    {
        /* ── 1. 기어: 개별 버튼 디바운스 + press-release 래치 ── */
        read_gear_raw();

        gear_p_buf[db_idx] = g_raw_p02_3;
        gear_r_buf[db_idx] = g_raw_p02_5;
        gear_n_buf[db_idx] = g_raw_p02_4;
        gear_d_buf[db_idx] = g_raw_p02_6;

        /* P 버튼 */
        if (gear_p_buf[0] == gear_p_buf[1] && gear_p_buf[1] == gear_p_buf[2])
        {
            boolean p_new = gear_p_buf[0];
            if (p_new && !gear_p_stable)
                gear_p_armed = TRUE;
            else if (!p_new && gear_p_armed)
            {
                gear_ok = GEAR_P;
                gear_p_armed = FALSE;
            }
            gear_p_stable = p_new;
        }

        /* R 버튼 */
        if (gear_r_buf[0] == gear_r_buf[1] && gear_r_buf[1] == gear_r_buf[2])
        {
            boolean r_new = gear_r_buf[0];
            if (r_new && !gear_r_stable)
                gear_r_armed = TRUE;
            else if (!r_new && gear_r_armed)
            {
                gear_ok = GEAR_R;
                gear_r_armed = FALSE;
            }
            gear_r_stable = r_new;
        }

        /* N 버튼 */
        if (gear_n_buf[0] == gear_n_buf[1] && gear_n_buf[1] == gear_n_buf[2])
        {
            boolean n_new = gear_n_buf[0];
            if (n_new && !gear_n_stable)
                gear_n_armed = TRUE;
            else if (!n_new && gear_n_armed)
            {
                gear_ok = GEAR_N;
                gear_n_armed = FALSE;
            }
            gear_n_stable = n_new;
        }

        /* D 버튼 */
        if (gear_d_buf[0] == gear_d_buf[1] && gear_d_buf[1] == gear_d_buf[2])
        {
            boolean d_new = gear_d_buf[0];
            if (d_new && !gear_d_stable)
                gear_d_armed = TRUE;
            else if (!d_new && gear_d_armed)
            {
                gear_ok = GEAR_D;
                gear_d_armed = FALSE;
            }
            gear_d_stable = d_new;
        }

        /* ── 도어 디바운싱 ── */
        door_sw_buf[db_idx] = read_door_switch_raw();
        db_idx = (db_idx + 1) % 3;

        /* gear override 해제: 물리 기어 변경 감지 */
        if (gear_ok != GEAR_ERROR)
        {
            if ((g_gear_override_active == TRUE) && (gear_ok != last_physical_gear))
                g_gear_override_active = FALSE;

            last_physical_gear = gear_ok;
        }

        if (door_sw_buf[0] == door_sw_buf[1] && door_sw_buf[1] == door_sw_buf[2])
        {
            door_ok = door_sw_buf[0] ? DOOR_CLOSE : DOOR_OPEN;
        }

        /* ── 2. 초음파 비블로킹 상태머신 (~12us 또는 즉시) ── */
        ultrasonic_update();
        int ultra_mm = latest_distance_mm;

        /* ── 3. 압력/ToF 취득 ── */
        uint16 pressure_adc_avg = read_pressure_adc_average();
        uint16 tof_mm_val = TofSensor_GetDistanceMm();

        g_pressure_raw = pressure_adc_avg;

        /* ── 4. 단위 변환 + 3샘플 중앙값 필터 ── */
        uint16 ultra_cm = ultra_mm_to_cm(ultra_mm);
        uint16 tof_cm = tof_mm_to_cm(tof_mm_val);
        uint32 press_g = pressure_adc_to_g(pressure_adc_avg);
        g_press_avg_g = press_g;

        ultra_buf[sensor_filt_idx] = ultra_cm;
        tof_buf[sensor_filt_idx] = tof_cm;
        press_buf[sensor_filt_idx] = press_g;
        sensor_filt_idx = (uint8)((sensor_filt_idx + 1u) % RAW_FILTER_SIZE);

        uint16 ultra_filt = median3(ultra_buf[0], ultra_buf[1], ultra_buf[2]);
        uint16 tof_filt = median3(tof_buf[0], tof_buf[1], tof_buf[2]);
        uint32 press_filt = median3_u32(press_buf[0], press_buf[1], press_buf[2]);

        /* ── 5. 점수 계산 + 최종 판정 ── */
        uint8 ru = score_ultrasonic(ultra_filt);
        uint8 rt = score_tof(tof_filt);
        uint8 rp = score_pressure(press_filt);
        DriverState driver = judge_driver_final(ru, rt, rp);

        /* ── 6. 디버깅 변수 갱신 ── */
        g_ultra_mm     = ultra_mm;
        g_pressure_adc = pressure_adc_avg;
        g_pressure_adc_1 = g_pressure_last_raw[0];
        g_pressure_adc_2 = g_pressure_last_raw[1];
        g_pressure_adc_3 = g_pressure_last_raw[2];
        g_tof_mm       = tof_mm_val;
        g_ultra_cm     = ultra_filt;
        g_tof_cm       = tof_filt;
        g_press_g      = press_filt;
        g_score_ultra  = ru;
        g_score_tof    = rt;
        g_score_press  = rp;
        g_pressure_present_debug = (rp >= 3u) ? TRUE : FALSE;
        g_gear_debug   = gear_ok;
        g_door_debug   = door_ok;
        g_driver_debug = driver;

        /* ── 7. LED 제어 ── */
        if (door_ok == DOOR_OPEN)
            Debug_LED_Set(1);
        else
            Debug_LED_Set(0);

        /* ── 8. 공유 데이터 갱신 ── */
        if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(5)) == pdTRUE)
        {
            GearState reportedGear = gear_ok;

            if (g_gear_override_active == TRUE)
                reportedGear = g_gear_override_state;

            g_sensor.driver = driver;
            g_sensor.door   = door_ok;
            if (reportedGear != GEAR_ERROR)
                g_sensor.gear = reportedGear;
            xSemaphoreGive(xSensorMutex);
        }

        /* ── 9. 정확한 10ms 주기 ── */
        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(10));
    }
}
