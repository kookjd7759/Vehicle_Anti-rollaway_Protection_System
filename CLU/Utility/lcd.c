#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lcd.h"

static char lcd_get_gear_char(uint8_t gear)
{
    switch (gear) {
    case LCD_GEAR_P: return 'P';
    case LCD_GEAR_R: return 'R';
    case LCD_GEAR_N: return 'N';
    case LCD_GEAR_D: return 'D';
    default:         return '?';
    }
}

static char lcd_get_driver_char(uint8_t driver)
{
    return (driver == LCD_DRIVER_PRESENT) ? 'O' : 'X';
}

static char lcd_get_door_char(uint8_t door)
{
    return (door == LCD_DOOR_CLOSE) ? 'O' : 'X';
}

static const char* lcd_get_risk_keyword(uint8_t warning, uint8_t brake)
{
    /* brake warning 상태 우선 */
    switch (brake) {
    case LCD_BRK_D:         return "W3";  /* RISK_D_BRAKE */
    case LCD_BRK_R:         return "W4";  /* RISK_R_BRAKE */
    case LCD_BRK_ROLLAWAY:  return "R2";  /* RISK_ROLLAWAY_BRAKE */
    default:                break;
    }

    /* brake watning 없으면 alert warning 기준 */
    switch (warning) {
    case LCD_WARN_LV1:       return "W1"; /* RISK_WARN_LV1 */
    case LCD_WARN_LV2:       return "W2"; /* RISK_WARN_LV2 */
    case LCD_WARN_ROLLAWAY:  return "R1"; /* RISK_ROLLAWAY_WARN */
    case LCD_WARN_NONE:
    default:                 return "OK"; /* RISK_NORMAL */
    }
}

static const char* lcd_get_brk_text(uint8_t brake)
{
    return (brake == LCD_BRK_NONE) ? "OFF" : "ON ";
}

static uint8_t lcd_get_motor_mode(uint8_t gear, uint8_t brake)
{
    /* brake가 걸리면 STOP */
    if (brake != LCD_BRK_NONE) {
        return 0; /* STOP */
    }

    switch (gear) {
    case LCD_GEAR_D: return 1; /* FWD */
    case LCD_GEAR_R: return 2; /* REV */
    case LCD_GEAR_N: return 3; /* NEUTRAL */
    case LCD_GEAR_P: return 4; /* PARK */
    default:         return 0; /* STOP */
    }
}

static uint8_t lcd_get_buzzer_mode(uint8_t warning, uint8_t brake)
{
    if (brake != LCD_BRK_NONE) {
        return 2; /* 연속 경고 */
    }

    switch (warning) {
    case LCD_WARN_LV1:       return 1; /* 단속 경고 */
    case LCD_WARN_LV2:       return 2; /* 연속 경고 */
    case LCD_WARN_ROLLAWAY:  return 2; /* 연속 경고 */
    case LCD_WARN_NONE:
    default:                 return 0; /* 부저 OFF */
    }
}

static uint8_t lcd_get_led_hazard(uint8_t brake)
{
    return (brake == LCD_BRK_NONE) ? 0 : 1;
}

static uint8_t lcd_get_led_brake(uint8_t brake)
{
    return (brake == LCD_BRK_NONE) ? 0 : 1;
}


/* --------------------------------
 * 32글자 LCD 문자열 생성 함수
 *
 * out_lcd1:
 * [0..15]  = "G ---km move    "
 * [16..31] = "driver - door - "
 *
 * out_lcd2:
 * [0..15]  = "RISK:__ BRK:___ "
 * [16..31] = "M:_ S:_ L:_ B:_ "
 *
 * 각 버퍼는 최소 33바이트 필요
 * -------------------------------- */
void lcd_make_strings(uint8_t warning, uint8_t brake, uint8_t gear,
    uint8_t door, uint8_t driver, uint8_t speed,
    char out_lcd1[33], char out_lcd2[33])
{
    char line1_1[17];
    char line1_2[17];
    char line2_1[17];
    char line2_2[17];

    char gear_ch = lcd_get_gear_char(gear);
    char driver_ch = lcd_get_driver_char(driver);
    char door_ch = lcd_get_door_char(door);

    const char* risk_txt = lcd_get_risk_keyword(warning, brake);
    const char* brk_txt = lcd_get_brk_text(brake);

    uint8_t motor_mode = lcd_get_motor_mode(gear, brake);
    uint8_t buzzer_mode = lcd_get_buzzer_mode(warning, brake);
    uint8_t led_hazard = lcd_get_led_hazard(brake);
    uint8_t led_brake = lcd_get_led_brake(brake);

    /* LCD 1 */
    /* "D ---km move    " */
    snprintf(line1_1, sizeof(line1_1),
        "%c %03ukm move    ",
        gear_ch, (unsigned int)speed);

    /* "driver - door - " */
    snprintf(line1_2, sizeof(line1_2),
        "driver %c door %c ",
        driver_ch, door_ch);

    /* LCD 2 */
    /* 예: "RISK:W1 BRK:OFF " / "RISK:R2 BRK:ON  " */
    snprintf(line2_1, sizeof(line2_1),
        "RISK:%2s BRK:%-3s ",
        risk_txt, brk_txt);

    /* "M:- S:- L:- B:- " */
    snprintf(line2_2, sizeof(line2_2),
        "M:%u S:%u L:%u B:%u ",
        (unsigned int)motor_mode,
        (unsigned int)buzzer_mode,
        (unsigned int)led_hazard,
        (unsigned int)led_brake);

    /* 32글자 고정 문자열 생성 */
    memcpy(&out_lcd1[0], line1_1, 16);
    memcpy(&out_lcd1[16], line1_2, 16);
    out_lcd1[32] = '\0';

    memcpy(&out_lcd2[0], line2_1, 16);
    memcpy(&out_lcd2[16], line2_2, 16);
    out_lcd2[32] = '\0';
}