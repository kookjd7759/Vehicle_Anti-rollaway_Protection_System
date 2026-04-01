/**
 * =============================================================================
 * VL53L0X Platform Layer for TC275 ShieldBuddy
 * =============================================================================
 *
 * 파일: vl53l0x_platform_tc275.c
 * 설명: TC275 AURIX iLLD를 이용한 VL53L0X Platform Layer 구현
 *
 * 이 파일은 VL53L0X API의 Platform Layer를 TC275의 I2C (IfxI2c)
 * 드라이버를 사용하여 구현합니다.
 *
 * 개발환경: AURIX Development Studio
 * 타겟 MCU: TC275 (ShieldBuddy)
 */

/* ─────────────────────────────────────────────
 *  헤더 파일
 * ───────────────────────────────────────────── */
#include "vl53l0x_platform.h"
#include "vl53l0x_api.h"

/* iLLD I2C 헤더 */
#include "IfxI2c_I2c.h"
#include "Ifx_Types.h"
#include "IfxPort.h"
#include "IfxStm.h"
#include "IfxPort_PinMap.h"

#include <string.h>

/* ─────────────────────────────────────────────
 *  전역 변수 / I2C 핸들
 * ───────────────────────────────────────────── */

/* I2C 모듈 핸들 */
static IfxI2c_I2c         g_i2cHandle;
static IfxI2c_I2c_Device  g_i2cDevice;

/* I2C 전송 버퍼 (레지스터 인덱스 + 데이터) */
#define I2C_BUFFER_SIZE  64
static uint8 g_i2cTxBuffer[I2C_BUFFER_SIZE];
static uint8 g_i2cRxBuffer[I2C_BUFFER_SIZE];

/* ─────────────────────────────────────────────
 *  I2C 초기화 (TC275 iLLD)
 * ───────────────────────────────────────────── */

/**
 * @brief TC275 I2C0 모듈을 초기화합니다.
 *
 * 이 함수를 main()에서 VL53L0X API 호출 전에 먼저 호출하세요.
 *
 * 핀 설정:
 *   - SDA: P02.5
 *   - SCL: P02.4
 *   - I2C 속도: 400kHz (Fast Mode)
 */
void VL53L0X_I2C_Init(void)
{
    /* I2C 모듈 기본 설정 */
    IfxI2c_I2c_Config i2cConfig;
    IfxI2c_I2c_initConfig(&i2cConfig, &MODULE_I2C0);

    /* 핀 할당 */
    const IfxI2c_Pins pins = {
        .scl = &IfxI2c0_SCL_P13_1_INOUT,   /* SCL 핀 */
        .sda = &IfxI2c0_SDA_P13_2_INOUT,   /* SDA 핀 */
        .padDriver = IfxPort_PadDriver_cmosAutomotiveSpeed1
    };
    i2cConfig.pins = &pins;

    /* I2C 속도 설정: 400kHz */
    i2cConfig.baudrate = 100000;

    /* I2C 모듈 초기화 */
    IfxI2c_I2c_initModule(&g_i2cHandle, &i2cConfig);

    /* I2C 디바이스(VL53L0X) 설정 */
    IfxI2c_I2c_deviceConfig deviceConfig;
    IfxI2c_I2c_initDeviceConfig(&deviceConfig, &g_i2cHandle);

    /* VL53L0X 기본 I2C 주소: 0x29 (7비트) */
    deviceConfig.deviceAddress = 0x29 << 1;  /* iLLD는 8비트 주소 사용 */

    IfxI2c_I2c_initDevice(&g_i2cDevice, &deviceConfig);
}


/* ─────────────────────────────────────────────
 *  밀리초 지연 함수
 * ───────────────────────────────────────────── */

/**
 * @brief 밀리초 단위 지연
 * @param ms 지연 시간 (밀리초)
 */
static void delay_ms(uint32 ms)
{
    uint32 tick = ms * 100000u;
    IfxStm_wait(tick);
}


/* ─────────────────────────────────────────────
 *  Platform Layer 함수 구현
 * ───────────────────────────────────────────── */

/**
 * @brief 다중 바이트 I2C 쓰기
 */
VL53L0X_Error VL53L0X_WriteMulti(VL53L0X_DEV Dev, uint8_t index,
                                  uint8_t *pdata, uint32_t count)
{
    VL53L0X_Error status = VL53L0X_ERROR_NONE;

    if ((count + 1) > I2C_BUFFER_SIZE)
        return VL53L0X_ERROR_INVALID_PARAMS;

    /* 첫 번째 바이트: 레지스터 인덱스 */
    g_i2cTxBuffer[0] = index;

    /* 나머지 바이트: 데이터 */
    memcpy(&g_i2cTxBuffer[1], pdata, count);

    /* I2C 전송 */
    if (IfxI2c_I2c_write(&g_i2cDevice, g_i2cTxBuffer, count + 1)
        == IfxI2c_I2c_Status_ok)
    {
        status = VL53L0X_ERROR_NONE;
    }
    else
    {
        status = VL53L0X_ERROR_CONTROL_INTERFACE;
    }

    return status;
}

/**
 * @brief 다중 바이트 I2C 읽기
 */
VL53L0X_Error VL53L0X_ReadMulti(VL53L0X_DEV Dev, uint8_t index,
                                 uint8_t *pdata, uint32_t count)
{
    VL53L0X_Error status = VL53L0X_ERROR_NONE;

    /* 먼저 레지스터 인덱스 전송 */
    g_i2cTxBuffer[0] = index;

    if (IfxI2c_I2c_write(&g_i2cDevice, g_i2cTxBuffer, 1)
        != IfxI2c_I2c_Status_ok)
    {
        return VL53L0X_ERROR_CONTROL_INTERFACE;
    }

    /* 데이터 읽기 */
    if (IfxI2c_I2c_read(&g_i2cDevice, pdata, count)
        == IfxI2c_I2c_Status_ok)
    {
        status = VL53L0X_ERROR_NONE;
    }
    else
    {
        status = VL53L0X_ERROR_CONTROL_INTERFACE;
    }

    return status;
}

/**
 * @brief 1바이트 쓰기
 */
VL53L0X_Error VL53L0X_WrByte(VL53L0X_DEV Dev, uint8_t index, uint8_t data)
{
    return VL53L0X_WriteMulti(Dev, index, &data, 1);
}

/**
 * @brief 2바이트 쓰기 (Big-Endian)
 *
 * VL53L0X 레지스터는 Big-Endian이므로 상위 바이트를 먼저 전송합니다.
 */
VL53L0X_Error VL53L0X_WrWord(VL53L0X_DEV Dev, uint8_t index, uint16_t data)
{
    uint8_t buf[2];
    buf[0] = (uint8_t)((data >> 8) & 0xFF);  /* MSB first */
    buf[1] = (uint8_t)(data & 0xFF);          /* LSB */

    return VL53L0X_WriteMulti(Dev, index, buf, 2);
}

/**
 * @brief 4바이트 쓰기 (Big-Endian)
 */
VL53L0X_Error VL53L0X_WrDWord(VL53L0X_DEV Dev, uint8_t index, uint32_t data)
{
    uint8_t buf[4];
    buf[0] = (uint8_t)((data >> 24) & 0xFF);
    buf[1] = (uint8_t)((data >> 16) & 0xFF);
    buf[2] = (uint8_t)((data >> 8) & 0xFF);
    buf[3] = (uint8_t)(data & 0xFF);

    return VL53L0X_WriteMulti(Dev, index, buf, 4);
}

/**
 * @brief 1바이트 읽기
 */
VL53L0X_Error VL53L0X_RdByte(VL53L0X_DEV Dev, uint8_t index, uint8_t *data)
{
    return VL53L0X_ReadMulti(Dev, index, data, 1);
}

/**
 * @brief 2바이트 읽기 (Big-Endian → uint16_t)
 */
VL53L0X_Error VL53L0X_RdWord(VL53L0X_DEV Dev, uint8_t index, uint16_t *data)
{
    VL53L0X_Error status;
    uint8_t buf[2];

    status = VL53L0X_ReadMulti(Dev, index, buf, 2);

    if (status == VL53L0X_ERROR_NONE)
    {
        *data = ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
    }

    return status;
}

/**
 * @brief 4바이트 읽기 (Big-Endian → uint32_t)
 */
VL53L0X_Error VL53L0X_RdDWord(VL53L0X_DEV Dev, uint8_t index, uint32_t *data)
{
    VL53L0X_Error status;
    uint8_t buf[4];

    status = VL53L0X_ReadMulti(Dev, index, buf, 4);

    if (status == VL53L0X_ERROR_NONE)
    {
        *data = ((uint32_t)buf[0] << 24)
              | ((uint32_t)buf[1] << 16)
              | ((uint32_t)buf[2] << 8)
              |  (uint32_t)buf[3];
    }

    return status;
}

/**
 * @brief 바이트 읽기-수정-쓰기
 *
 * 레지스터를 읽고, AND/OR 마스크를 적용한 후 다시 씁니다.
 */
VL53L0X_Error VL53L0X_UpdateByte(VL53L0X_DEV Dev, uint8_t index,
                                  uint8_t AndData, uint8_t OrData)
{
    VL53L0X_Error status;
    uint8_t data;

    status = VL53L0X_RdByte(Dev, index, &data);

    if (status == VL53L0X_ERROR_NONE)
    {
        data = (data & AndData) | OrData;
        status = VL53L0X_WrByte(Dev, index, data);
    }

    return status;
}

/**
 * @brief 폴링 지연 함수
 *
 * VL53L0X API가 측정 완료를 기다릴 때 호출합니다.
 * 기본값: 약 2ms 지연
 */
VL53L0X_Error VL53L0X_PollingDelay(VL53L0X_DEV Dev)
{
    delay_ms(2);
    return VL53L0X_ERROR_NONE;
}
