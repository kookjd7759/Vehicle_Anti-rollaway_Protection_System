/**********************************************************************************************************************
 * \file sensor_driver.c
 * \brief VAPS MAIN ECU - 센서 드라이버 (ERU 인터럽트 기반 초음파)
 *
 * Task_Sensor (10ms 주기):
 *   - 기어/도어 GPIO 읽기 + 디바운싱       ~수 us
 *   - 초음파: 결과 확인 + Trigger (비블로킹) ~12 us
 *   - 5샘플 순환버퍼 + 중앙값/EMA 필터
 *   - 운전자 퓨전 판정
 *   - LED 제어
 *   - 총 실행 시간: ~1ms 이내 (10ms 주기 정확히 유지)
 *********************************************************************************************************************/
#include "App/sensor_data.h"
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
 *  STM 타이머 (타임아웃 확인용)
 * ══════════════════════════════════════ */
#define STM_FREQ        100000000u
#define ULTRA_TIMEOUT_TICKS  5000000u  /* 50ms */

#define PRESSURE_ON_TH   ((uint16)2500U)
#define PRESSURE_OFF_TH  ((uint16)2200U)

static uint32 timer_now(void)
{
    return MODULE_STM0.TIM0.U;
}

/* ══════════════════════════════════════
 *  압력 센서 EVADC
 * ══════════════════════════════════════ */
static IfxEvadc_Adc g_evadc;
static IfxEvadc_Adc_Group g_adc_group;
static IfxEvadc_Adc_Channel g_adc_channel;

static void init_pressure_sensor(void)
{
    IfxEvadc_Adc_Config adc_config;
    IfxEvadc_Adc_GroupConfig group_config;
    IfxEvadc_Adc_ChannelConfig channel_config;

    IfxEvadc_Adc_initModuleConfig(&adc_config, &MODULE_EVADC);
    IfxEvadc_Adc_initModule(&g_evadc, &adc_config);

    IfxEvadc_Adc_initGroupConfig(&group_config, &g_evadc);
    group_config.groupId = IfxEvadc_GroupId_8;
    group_config.master = IfxEvadc_GroupId_8;
    group_config.arbiter.requestSlotQueue0Enabled = TRUE;
    group_config.queueRequest[0].triggerConfig.gatingMode = IfxEvadc_GatingMode_always;
    IfxEvadc_Adc_initGroup(&g_adc_group, &group_config);

    IfxEvadc_Adc_initChannelConfig(&channel_config, &g_adc_group);
    channel_config.channelId = IfxEvadc_ChannelId_6;
    channel_config.resultRegister = IfxEvadc_ChannelResult_6;
    IfxEvadc_Adc_initChannel(&g_adc_channel, &channel_config);

    IfxEvadc_Adc_addToQueue(&g_adc_channel, IfxEvadc_RequestSource_queue0, IFXEVADC_QUEUE_REFILL);
    IfxEvadc_Adc_startQueue(&g_adc_group, IfxEvadc_RequestSource_queue0);
}

static uint16 read_pressure_adc(void)
{
    Ifx_EVADC_G_RES result;
    result = IfxEvadc_Adc_getResult(&g_adc_channel);

    if (result.B.VF)
        return (uint16)result.B.RESULT;

    return g_pressure_raw;
}

/* ══════════════════════════════════════
 *  초음파 필터 설정
 * ══════════════════════════════════════ */
#define SAMPLE_COUNT    5
#define MIN_DIST_MM     20
#define MAX_DIST_MM     4000
#define MAX_JUMP_MM     400

static int prev_distance_mm = 0;

/* 초음파 비블로킹 상태 머신 */
typedef enum {
    US_TRIGGER,     /* Trig 전송 */
    US_WAIT,        /* ISR 완료 대기 */
    US_COOLDOWN     /* 다음 측정 전 대기 (에코 잔향 소멸) */
} UltraSensorPhase;

static UltraSensorPhase ultra_phase = US_TRIGGER;
static uint32 ultra_trigger_tick = 0;
static uint32 samples_us[SAMPLE_COUNT];
static int sample_idx = 0;
static int filtered_distance_mm = 0;

/* ══════════════════════════════════════
 *  디바운싱 버퍼
 * ══════════════════════════════════════ */
static GearState gear_buf[3]  = {GEAR_P, GEAR_P, GEAR_P};
static boolean door_sw_buf[3] = {0, 0, 0};
static uint8 db_idx = 0;

/* ══════════════════════════════════════
 *  디버깅용 전역 변수
 * ══════════════════════════════════════ */
volatile int        g_ultra_mm       = 0;
volatile uint16     g_pressure_adc   = 0;
volatile uint16     g_pressure_raw   = 0;
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
volatile boolean g_pressure_present_debug = FALSE;


/* ══════════════════════════════════════
 *  기어 읽기
 * ══════════════════════════════════════ */
static GearState read_gear_raw(void)
{
    g_raw_p02_3 = IfxPort_getPinState(&MODULE_P02, 3);
    g_raw_p02_5 = IfxPort_getPinState(&MODULE_P02, 5);
    g_raw_p02_4 = IfxPort_getPinState(&MODULE_P02, 4);
    g_raw_p02_6 = IfxPort_getPinState(&MODULE_P02, 6);

    boolean p = g_raw_p02_3;
    boolean r = g_raw_p02_5;
    boolean n = g_raw_p02_4;
    boolean d = g_raw_p02_6;

    g_pin_gear_p = p;
    g_pin_gear_r = r;
    g_pin_gear_n = n;
    g_pin_gear_d = d;
    g_gear_count = p + r + n + d;

    int count = p + r + n + d;
    if (count != 1) return GEAR_ERROR;
    if (p) return GEAR_P;
    if (r) return GEAR_R;
    if (n) return GEAR_N;
    if (d) return GEAR_D;
    return GEAR_ERROR;
}

/* ══════════════════════════════════════
 *  도어 읽기
 * ══════════════════════════════════════ */
static boolean read_door_switch_raw(void)
{
    return IfxPort_getPinState(&MODULE_P02, 7) ? TRUE : FALSE;
}

/* ══════════════════════════════════════
 *  초음파 필터 함수
 * ══════════════════════════════════════ */
static int us_to_mm(uint32 echo_us)
{
    return (int)((echo_us * 10 + 29) / 58);
}

static void sort_int(int *arr, int n)
{
    int i, j, tmp;
    for (i = 0; i < n - 1; i++)
        for (j = i + 1; j < n; j++)
            if (arr[i] > arr[j])
            {
                tmp = arr[i];
                arr[i] = arr[j];
                arr[j] = tmp;
            }
}

/* 5샘플 수집 완료 시 호출 → 필터링된 거리 반환 */
static int apply_filter(uint32 echo_us_arr[SAMPLE_COUNT])
{
    int valid_mm[SAMPLE_COUNT];
    int valid_count = 0;
    int i;

    for (i = 0; i < SAMPLE_COUNT; i++)
    {
        int dist_mm;
        if (echo_us_arr[i] == 0) continue;
        dist_mm = us_to_mm(echo_us_arr[i]);
        if (dist_mm < MIN_DIST_MM || dist_mm > MAX_DIST_MM) continue;
        valid_mm[valid_count++] = dist_mm;
    }

    if (valid_count == 0)
        return prev_distance_mm;

    sort_int(valid_mm, valid_count);
    {
        int median_mm = valid_mm[valid_count / 2];

        if (prev_distance_mm != 0)
        {
            int diff = median_mm - prev_distance_mm;
            if (diff < 0) diff = -diff;
            if (diff > MAX_JUMP_MM)
                median_mm = prev_distance_mm;
        }

        {
            int filtered;
            if (prev_distance_mm == 0)
                filtered = median_mm;
            else
                filtered = (prev_distance_mm * 7 + median_mm * 3) / 10;

            prev_distance_mm = filtered;
            return filtered;
        }
    }
}

/* ══════════════════════════════════════
 *  초음파 비블로킹 상태 머신
 *
 *  10ms마다 호출됨. 상태에 따라 동작:
 *  US_TRIGGER  → Trig 전송 (12us)
 *  US_WAIT     → ISR 완료 확인 (즉시 리턴)
 *  US_COOLDOWN → 에코 잔향 소멸 대기 (10ms)
 *
 *  5회 측정 후 필터 적용 → filtered_distance_mm 갱신
 * ══════════════════════════════════════ */
static void ultrasonic_update(void)
{
    switch (ultra_phase)
    {
    case US_TRIGGER:
        Ultrasonic_Trigger();                   /* ~12us */
        ultra_trigger_tick = timer_now();
        ultra_phase = US_WAIT;
        break;

    case US_WAIT:
        if (Ultrasonic_IsDone())
        {
            /* 측정 완료 → 샘플 저장 */
            samples_us[sample_idx] = Ultrasonic_GetEchoUs();
            sample_idx++;

            if (sample_idx >= SAMPLE_COUNT)
            {
                /* 5샘플 수집 완료 → 필터 적용 */
                filtered_distance_mm = apply_filter(samples_us);
                sample_idx = 0;
            }

            ultra_trigger_tick = timer_now();
            ultra_phase = US_COOLDOWN;
        }
        else if ((timer_now() - ultra_trigger_tick) > ULTRA_TIMEOUT_TICKS)
        {
            /* 50ms 타임아웃 → 0으로 기록 */
            samples_us[sample_idx] = 0;
            sample_idx++;

            if (sample_idx >= SAMPLE_COUNT)
            {
                filtered_distance_mm = apply_filter(samples_us);
                sample_idx = 0;
            }

            ultra_trigger_tick = timer_now();
            ultra_phase = US_COOLDOWN;
        }
        /* else: 아직 측정 중 → 다음 10ms에 다시 확인 */
        break;

    case US_COOLDOWN:
        /* 에코 잔향 소멸 대기: 10ms (= 1회 태스크 주기) */
        if ((timer_now() - ultra_trigger_tick) > (STM_FREQ / 100u))
        {
            ultra_phase = US_TRIGGER;
        }
        break;
    }
}

/* ══════════════════════════════════════
 *  운전자 판정
 * ══════════════════════════════════════ */
static DriverState judge_driver(boolean pressure_present, uint16 tof_mm, int ultra_mm)
{
    if (pressure_present == TRUE)
        return DRIVER_SEATED;

    if (tof_mm < 600 || ultra_mm < 600)
        return DRIVER_UNCERTAIN;

    return DRIVER_ABSENT;
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
    static DoorState door_ok = DOOR_CLOSE;
    static boolean door_sw_stable = FALSE;
    static boolean door_press_armed = FALSE;
    static boolean pressure_present = FALSE;

    init_pressure_sensor();

    while (1)
    {
        /* ── 1. 기어/도어 디바운싱 (~수 us) ── */
        gear_buf[db_idx] = read_gear_raw();
        door_sw_buf[db_idx] = read_door_switch_raw();
        db_idx = (db_idx + 1) % 3;

        GearState gear_new = GEAR_ERROR;
        if (gear_buf[0] == gear_buf[1] && gear_buf[1] == gear_buf[2])
            gear_new = gear_buf[0];
        if (gear_new != GEAR_ERROR)
            gear_ok = gear_new;

        if (door_sw_buf[0] == door_sw_buf[1] && door_sw_buf[1] == door_sw_buf[2])
        {
            boolean door_sw_new = door_sw_buf[0];

            if (door_sw_new != door_sw_stable)
            {
                if (door_sw_new == TRUE)
                {
                    door_press_armed = TRUE;
                }
                else if (door_press_armed == TRUE)
                {
                    door_ok = (door_ok == DOOR_CLOSE) ? DOOR_OPEN : DOOR_CLOSE;
                    door_press_armed = FALSE;
                }

                door_sw_stable = door_sw_new;
            }
        }

        /* ── 2. 초음파 비블로킹 상태머신 (~12us 또는 즉시) ── */
        ultrasonic_update();
        int ultra_mm = filtered_distance_mm;

        /* ── 3. 압력/ToF ── */
        uint16 pressure_adc = read_pressure_adc();
        uint16 tof_mm = 999;

        g_pressure_raw = pressure_adc;

        if (pressure_present == FALSE)
        {
            if (pressure_adc >= PRESSURE_ON_TH){
                g_pressure_present_debug = TRUE;
                pressure_present = TRUE;
            }
        }
        else
        {
            if (pressure_adc <= PRESSURE_OFF_TH){
                pressure_present = FALSE;
                g_pressure_present_debug = FALSE;

            }
        }

        /* ── 4. 운전자 판정 ── */
        DriverState driver = judge_driver(pressure_present, tof_mm, ultra_mm);

        /* ── 5. 디버깅 변수 갱신 ── */
        g_ultra_mm     = ultra_mm;
        g_pressure_adc = pressure_adc;
        g_tof_mm       = tof_mm;
        g_gear_debug   = gear_ok;
        g_door_debug   = door_ok;
        g_driver_debug = driver;

        /* ── 6. LED 제어 ── */
        if (door_ok == DOOR_OPEN)
            Debug_LED_Set(1);
        else
            Debug_LED_Set(0);

        /*if (gear_ok == GEAR_P)
                    Debug_LED_Set(1);
                else
                    Debug_LED_Set(0);
        if (gear_ok == GEAR_N)
                    Debug_LED_Set(1);
                else
                    Debug_LED_Set(0);
        if (gear_ok == GEAR_R)
                    Debug_LED_Set(1);
                else
                    Debug_LED_Set(0);
        if (gear_ok == GEAR_D)
                    Debug_LED_Set(1);
                 else
                    Debug_LED_Set(0);*/


        /* ── 7. 공유 데이터 갱신 ── */
        if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(5)) == pdTRUE)
        {
            g_sensor.driver = driver;
            g_sensor.door   = door_ok;
            if (gear_ok != GEAR_ERROR)
                g_sensor.gear = gear_ok;
            xSemaphoreGive(xSensorMutex);
        }

        /* ── 8. 정확한 10ms 주기 ── */
        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(10));
    }
}
