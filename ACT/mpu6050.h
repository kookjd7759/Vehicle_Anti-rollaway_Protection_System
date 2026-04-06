#ifndef MPU6050_H
#define MPU6050_H

#include "Ifx_Types.h"
#include "IfxI2c_I2c.h"

/* ── MPU-6050 I2C 주소 ── */
#define MPU6050_I2C_ADDR         0x68U

/* ── MPU-6050 레지스터 ── */
#define MPU6050_REG_PWR_MGMT_1   0x6BU
#define MPU6050_REG_ACCEL_XOUT_H 0x3BU
#define MPU6050_REG_ACCEL_CONFIG 0x1CU

/* ── 가속도 전역변수 ── */
extern volatile sint8 g_accelX;
extern volatile sint8 g_accelY;
extern volatile sint8 g_accelZ;
extern volatile uint8 g_accelValid;   /* 0 = 읽기 실패 */

/* ── 공개 함수 ── */
void initMPU6050(void);
void updateAccel(void);

#endif /* MPU6050_H */
