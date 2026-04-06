#include "mpu6050.h"
#include "IfxI2c_I2c.h"

/* ── 전역 정의 ── */
volatile sint8 g_accelX    = 0;
volatile sint8 g_accelY    = 0;
volatile sint8 g_accelZ    = 0;
volatile uint8 g_accelValid = 0U;

/* ── 내부 I2C 객체 ── */
static IfxI2c_I2c        g_i2cHandle;
static IfxI2c_I2c_Device g_mpu6050Dev;

/* ── 내부: float 클램핑 헬퍼 ── */
static float clampf(float v, float lo, float hi)
{
    if (v > hi) return hi;
    if (v < lo) return lo;
    return v;
}

/* ── 내부: 레지스터 1바이트 쓰기 ── */
static uint8 mpu6050WriteReg(uint8 reg, uint8 value)
{
    uint8 buf[2] = { reg, value };
    if (IfxI2c_I2c_write2(&g_mpu6050Dev, buf, 2) != IfxI2c_I2c_Status_ok)
        return 0U;
    return 1U;
}

/* ── 내부: 연속 레지스터 읽기 ── */
static uint8 mpu6050ReadRegs(uint8 reg, uint8 *pData, uint8 len)
{
    if (IfxI2c_I2c_write2(&g_mpu6050Dev, &reg, 1) != IfxI2c_I2c_Status_ok)
        return 0U;
    if (IfxI2c_I2c_read2(&g_mpu6050Dev, pData, len) != IfxI2c_I2c_Status_ok)
        return 0U;
    return 1U;
}

/* ── 공개 함수 ── */
void initMPU6050(void)
{
    /* I2C 모듈 초기화 */
    IfxI2c_I2c_Config i2cConfig;
    IfxI2c_I2c_initConfig(&i2cConfig, &MODULE_I2C0);

    /* SCL → P13.1 / SDA → P13.2 (TC375 Lite Kit 공식 핀) */
    static const IfxI2c_Pins i2cPins = {
        &IfxI2c0_SCL_P13_1_INOUT,
        &IfxI2c0_SDA_P13_2_INOUT,
        IfxPort_PadDriver_cmosAutomotiveSpeed1
    };
    i2cConfig.pins     = &i2cPins;
    i2cConfig.baudrate = 400000U;   /* 400 kHz Fast Mode */

    IfxI2c_I2c_initModule(&g_i2cHandle, &i2cConfig);

    /* MPU-6050 디바이스 설정 */
    IfxI2c_I2c_deviceConfig devConfig;
    IfxI2c_I2c_initDeviceConfig(&devConfig, &g_i2cHandle);
    devConfig.deviceAddress = MPU6050_I2C_ADDR << 1U;  /* 7bit → 8bit */
    IfxI2c_I2c_initDevice(&g_mpu6050Dev, &devConfig);

    /* 슬립 해제 */
    mpu6050WriteReg(MPU6050_REG_PWR_MGMT_1, 0x00U);
    /* 가속도 범위 ±2g */
    mpu6050WriteReg(MPU6050_REG_ACCEL_CONFIG, 0x00U);

    g_accelValid = 1U;
}

void updateAccel(void)
{
    uint8  raw[6];
    sint16 rawX, rawY, rawZ;
    float  gX, gY, gZ;

    if (mpu6050ReadRegs(MPU6050_REG_ACCEL_XOUT_H, raw, 6U) == 0U)
    {
        g_accelValid = 0U;
        /* 읽기 실패 시 0으로 안전값 유지 */
        g_accelX = 0; g_accelY = 0; g_accelZ = 0;
        return;
    }

    /* 16bit raw 조합 (big-endian → sint16) */
    rawX = (sint16)(((uint16)raw[0] << 8U) | (uint16)raw[1]);
    rawY = (sint16)(((uint16)raw[2] << 8U) | (uint16)raw[3]);
    rawZ = (sint16)(((uint16)raw[4] << 8U) | (uint16)raw[5]);

    /* ±2g 스케일 변환 (LSB = 16384 /g) */
    gX = (float)rawX / 16384.0f;
    gY = (float)rawY / 16384.0f;
    gZ = (float)rawZ / 16384.0f;

    /* ±1g 클램핑 후 *100 → sint8 변환 */
    g_accelX = (sint8)((clampf(gX, -1.0f, 1.0f) * 100.0f)) ;
    g_accelY = (sint8)(clampf(gY, -1.0f, 1.0f) * 100.0f);
    g_accelZ = (sint8)(clampf(gZ, -1.0f, 1.0f) * 100.0f);

    g_accelValid = 1U;
}
