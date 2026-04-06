/**********************************************************************************************************************
 * \file state_machine.c
 *********************************************************************************************************************/

#include "App/sensor_data.h"
#include "FreeRTOS.h"
#include "task.h"

#define VEHICLE_MOVING_THRESHOLD_KMH 1.0f
#define VEHICLE_ACCEL_THRESHOLD      2

static RiskLevel current_state = RISK_NORMAL;
static TickType_t door_open_start = 0;

static boolean is_auto_brake_state(RiskLevel state)
{
    return (state == RISK_D_BRAKE) ||
           (state == RISK_R_BRAKE) ||
           (state == RISK_ROLLAWAY_BRAKE);
}

static MotionState derive_motion(const SensorData *sensor)
{
    boolean moving_by_speed;
    boolean moving_by_accel;

    if (!sensor->act_feedback_alive)
        return MOTION_STOPPED;

    moving_by_speed = (sensor->speed_kmh >= VEHICLE_MOVING_THRESHOLD_KMH);
    moving_by_accel = (sensor->accel_x >= VEHICLE_ACCEL_THRESHOLD) ||
                      (sensor->accel_x <= -VEHICLE_ACCEL_THRESHOLD) ||
                      (sensor->accel_y >= VEHICLE_ACCEL_THRESHOLD) ||
                      (sensor->accel_y <= -VEHICLE_ACCEL_THRESHOLD);

    if (moving_by_speed || moving_by_accel)
        return MOTION_MOVING;

    return MOTION_STOPPED;
}

static boolean is_act_brake_engaged(const SensorData *sensor)
{
    if (!sensor->act_feedback_alive)
        return FALSE;

    return (sensor->act_brake_state == BRAKE_CMD_HOLD) ||
           (sensor->act_brake_state == BRAKE_CMD_FORCE);
}

static BrakeCommand select_brake_cmd(RiskLevel state, const SensorData *sensor)
{
    if (!is_auto_brake_state(state))
        return BRAKE_CMD_RELEASE;

    if (!is_act_brake_engaged(sensor))
        return BRAKE_CMD_FORCE;

    return BRAKE_CMD_HOLD;
}

/* ── 상태 전이 ── */
static RiskLevel evaluate(const SensorData *s, RiskLevel prev)
{
    boolean seated = (s->driver == DRIVER_SEATED);
    boolean absent = (s->driver == DRIVER_ABSENT);
    boolean door_open = (s->door == DOOR_OPEN);
    boolean door_close = (s->door == DOOR_CLOSE);
    boolean gear_p = (s->gear == GEAR_P);
    boolean gear_d = (s->gear == GEAR_D);
    boolean gear_r = (s->gear == GEAR_R);
    boolean gear_dr = gear_d || gear_r;
    boolean gear_n = (s->gear == GEAR_N);
    boolean moving = (derive_motion(s) == MOTION_MOVING);
    TickType_t now = xTaskGetTickCount();

    if (door_open && door_open_start == 0)
        door_open_start = now;
    else if (!door_open)
        door_open_start = 0;

    uint32 door_open_ms = 0;
    if (door_open_start > 0)
        door_open_ms = (now - door_open_start) * portTICK_PERIOD_MS;

    /* SAF-01: P단 → 즉시 NORMAL
     * 단, 자동제동 상태(D_BRAKE/R_BRAKE/ROLLAWAY_BRAKE)에서는
     * 기어가 P로 보고되더라도 이 조건으로 탈출 불가.
     * 복귀 조건은 착석+도어닫힘(SAF-02)만 허용. */
    if (gear_p && !is_auto_brake_state(prev))
        return RISK_NORMAL;

    /* SAF-02: 착석 + 도어닫힘 → NORMAL (자동제동 상태 포함 모든 상태에서 복귀) */
    if (seated && door_close)
        return RISK_NORMAL;

    /* D/R단 + 운전자 부재는 즉시 자동제동 */
    if (gear_d && absent)
        return RISK_D_BRAKE;
    if (gear_r && absent)
        return RISK_R_BRAKE;

    /* N단 + 차량 이동 + 운전자 부재는 즉시 롤어웨이 제동 */
    if (gear_n && moving && absent)
        return RISK_ROLLAWAY_BRAKE;

    /* N단 + 도어 열림은 롤어웨이 경고 */
    if (gear_n && door_open)
        return RISK_ROLLAWAY_WARN;

    switch (prev)
    {
    case RISK_NORMAL:
        if (gear_dr && door_open)
            return RISK_WARN_LV1;
        return RISK_NORMAL;

    case RISK_WARN_LV1:
        if (door_close)
            return RISK_NORMAL;
        if ((!gear_dr) && (!gear_n))
            return RISK_NORMAL;
        if (door_open_ms >= 2000U)
            return RISK_WARN_LV2;
        return RISK_WARN_LV1;

    case RISK_WARN_LV2:
        if (door_close)
            return RISK_NORMAL;
        return RISK_WARN_LV2;

    case RISK_D_BRAKE:
        return RISK_D_BRAKE;

    case RISK_R_BRAKE:
        return RISK_R_BRAKE;

    case RISK_ROLLAWAY_WARN:
        if (door_close)
            return RISK_NORMAL;
        if (gear_dr)
            return RISK_WARN_LV1;
        if (moving && absent)
            return RISK_ROLLAWAY_BRAKE;
        return RISK_ROLLAWAY_WARN;

    case RISK_ROLLAWAY_BRAKE:
        return RISK_ROLLAWAY_BRAKE;

    default:
        return RISK_NORMAL;
    }
}

/* ── 명령 생성 ── */
static void make_command(RiskLevel state, const SensorData *sensor, ControlCommand *cmd)
{
    cmd->risk_level = state;
    cmd->brake_cmd = select_brake_cmd(state, sensor);
}

/* ══════════════════════════════════════
 *  Task_Judge: 50ms 주기
 * ══════════════════════════════════════ */
void Task_Judge(void *param)
{
    TickType_t xLastWake = xTaskGetTickCount();
    IFX_UNUSED_PARAMETER(param);

    while (1)
    {
        SensorData local = {
            DRIVER_ABSENT,
            GEAR_P,
            DOOR_CLOSE,
            0.0f,
            0,
            0,
            0,
            MOTION_STOPPED,
            BRAKE_CMD_RELEASE,
            FALSE
        };

        /* 센서 데이터 복사 */
        if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(5)) == pdTRUE)
        {
            g_sensor.motion = derive_motion(&g_sensor);
            local = g_sensor;
            xSemaphoreGive(xSensorMutex);
        }

        /* 상태 전이 + 명령 생성 */
        current_state = evaluate(&local, current_state);

        ControlCommand cmd;
        make_command(current_state, &local, &cmd);

        /* 명령 갱신 */
        if (xSemaphoreTake(xCommandMutex, pdMS_TO_TICKS(5)) == pdTRUE)
        {
            g_command = cmd;
            xSemaphoreGive(xCommandMutex);
        }

        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(50));
    }
}
