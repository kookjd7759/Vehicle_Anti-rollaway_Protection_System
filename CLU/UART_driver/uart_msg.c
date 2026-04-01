/**********************************************************************************************************************
 * \file uart_msg.c
 *********************************************************************************************************************/
#include "uart_msg.h"

#include "IfxCpu.h"
#include "IfxCpu_Irq.h"
#include "IfxAsclin_Asc.h"
#include "IfxPort.h"

#include <string.h>
#include <stdio.h>

#define UART_BAUDRATE           115200
#define ASC_TX_BUFFER_SIZE      64
#define ASC_RX_BUFFER_SIZE      64

#define ISR_PRIORITY_ASC_2_TX   18
#define ISR_PRIORITY_ASC_2_RX   19
#define ISR_PRIORITY_ASC_2_EX   20

static IfxAsclin_Asc g_ascHandle;

/* FIFO buffers */
static uint8 g_ascTxBuffer[ASC_TX_BUFFER_SIZE + sizeof(Ifx_Fifo) + 8];
static uint8 g_ascRxBuffer[ASC_RX_BUFFER_SIZE + sizeof(Ifx_Fifo) + 8];

/* ---------------- UART ISR ---------------- */
IFX_INTERRUPT(ASC_Tx2IntHandler, 0, ISR_PRIORITY_ASC_2_TX);
IFX_INTERRUPT(ASC_Rx2IntHandler, 0, ISR_PRIORITY_ASC_2_RX);
IFX_INTERRUPT(ASC_Err2IntHandler, 0, ISR_PRIORITY_ASC_2_EX);

void ASC_Tx2IntHandler(void)
{
    IfxAsclin_Asc_isrTransmit(&g_ascHandle);
}

void ASC_Rx2IntHandler(void)
{
    IfxAsclin_Asc_isrReceive(&g_ascHandle);
}

void ASC_Err2IntHandler(void)
{
    IfxAsclin_Asc_isrError(&g_ascHandle);
}

/* ---------------- UART Init ---------------- */
void Driver_Asc0_Init(void)
{
    IfxAsclin_Asc_Config ascConfig;
    IfxAsclin_Asc_initModuleConfig(&ascConfig, &MODULE_ASCLIN2);

    ascConfig.baudrate.prescaler    = 1;
    ascConfig.baudrate.baudrate     = UART_BAUDRATE;
    ascConfig.baudrate.oversampling = IfxAsclin_OversamplingFactor_4;

    ascConfig.interrupt.txPriority    = ISR_PRIORITY_ASC_2_TX;
    ascConfig.interrupt.rxPriority    = ISR_PRIORITY_ASC_2_RX;
    ascConfig.interrupt.erPriority    = ISR_PRIORITY_ASC_2_EX;
    ascConfig.interrupt.typeOfService = IfxSrc_Tos_cpu0;

    ascConfig.txBuffer     = g_ascTxBuffer;
    ascConfig.txBufferSize = ASC_TX_BUFFER_SIZE;
    ascConfig.rxBuffer     = g_ascRxBuffer;
    ascConfig.rxBufferSize = ASC_RX_BUFFER_SIZE;

    const IfxAsclin_Asc_Pins pins = {
        NULL_PTR,                    IfxPort_InputMode_pullUp,
        &IfxAsclin2_RXE_P33_8_IN,     IfxPort_InputMode_pullUp,
        NULL_PTR,                    IfxPort_OutputMode_pushPull,
        &IfxAsclin2_TX_P33_9_OUT,    IfxPort_OutputMode_pushPull,
        IfxPort_PadDriver_cmosAutomotiveSpeed1
    };
    ascConfig.pins = &pins;

    IfxAsclin_Asc_initModule(&g_ascHandle, &ascConfig);
}

/* ---------------- UART Text Send ---------------- */
void UART_sendText(const char *text)
{
    Ifx_SizeT count = (Ifx_SizeT)strlen(text);
    IfxAsclin_Asc_write(&g_ascHandle, (uint8 *)text, &count, TIME_INFINITE);
}

/* ---------------- UART Text Send ---------------- */
void sendData(int warning, int brake, int gear, int door, int driver, int speed)
{
    unsigned short status = 0;
    char binStr[17];
    int i;

    status |= ((warning & 0x03) << 14);
    status |= ((brake   & 0x03) << 12);
    status |= ((gear    & 0x03) << 10);
    status |= ((door    & 0x01) << 9);
    status |= ((driver  & 0x01) << 8);
    status |=  (speed   & 0xFF);

    for (i = 0; i < 16; i++)
    {
        binStr[i] = ((status >> (15 - i)) & 1) ? '1' : '0';
    }
    binStr[16] = '\0';

    UART_sendText(binStr);
    UART_sendText("\r\n");
}
