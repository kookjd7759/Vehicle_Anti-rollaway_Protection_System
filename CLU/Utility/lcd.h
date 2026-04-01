#ifndef LCD_FORMAT_H
#define LCD_FORMAT_H

#include <stdint.h>

/* -----------------------------
 * 입력값 코드 정의
 * ----------------------------- */

 /* warning_type (2bit)
  * 0: 경고 없음
  * 1: 1차 경고
  * 2: 강화 경고
  * 3: Rollaway 경고
  */
#define LCD_WARN_NONE       0
#define LCD_WARN_LV1        1
#define LCD_WARN_LV2        2
#define LCD_WARN_ROLLAWAY   3

  /* brake_type (2bit)
   * 0: 제동 없음
   * 1: D단 제동
   * 2: R단 제동
   * 3: Rollaway 제동
   */
#define LCD_BRK_NONE        0
#define LCD_BRK_D           1
#define LCD_BRK_R           2
#define LCD_BRK_ROLLAWAY    3

   /* gear_state (2bit)
    * 0: P
    * 1: R
    * 2: N
    * 3: D
    */
#define LCD_GEAR_P          0
#define LCD_GEAR_R          1
#define LCD_GEAR_N          2
#define LCD_GEAR_D          3

    /* door_state (1bit)
     * 0: 닫힘
     * 1: 열림
     */
#define LCD_DOOR_CLOSE      0
#define LCD_DOOR_OPEN       1

     /* driver_present (1bit)
      * 0: 운전자 부재
      * 1: 운전자 존재
      */
#define LCD_DRIVER_ABSENT   0
#define LCD_DRIVER_PRESENT  1

      /* --------------------------------
       * 32글자 LCD 문자열 생성 함수
       *
       * out_lcd1, out_lcd2는 각각 최소 33바이트 필요
       * -------------------------------- */
void lcd_make_strings(uint8_t warning, uint8_t brake, uint8_t gear,
    uint8_t door, uint8_t driver, uint8_t speed,
    char out_lcd1[33], char out_lcd2[33]);

#endif /* LCD_FORMAT_H */