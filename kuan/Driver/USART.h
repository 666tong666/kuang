#ifndef __USART_H
#define __USART_H

#include "stm32f4xx.h"
#include <stdio.h>

void USART_Config(uint32_t baud);
void Usart_SendBytes(USART_TypeDef * pUSARTx, uint8_t *buf,uint32_t len);
void Usart_SendArray( USART_TypeDef * pUSARTx, uint8_t *array, uint16_t num);
void UART3_Config(uint32_t BaudRate);
void Usart_SendByte( USART_TypeDef * pUSARTx, uint8_t ch);
void Usart_SendHalfWord( USART_TypeDef * pUSARTx, uint16_t ch);
void Usart_SendString(USART_TypeDef *pUSARTx, char *str);


#define U1_GROUP GPIOA
#define U1_TX GPIO_Pin_9
#define U1_RX GPIO_Pin_10
extern char u1_data;

/* ---------------- USART2 + DX-BT24 蓝牙模块 ---------------- */
/* 引脚: PA2-TX(接BT24 RXD)  PA3-RX(接BT24 TXD)
 * DX-BT24 默认参数: 9600bps / 8 / N / 1, BLE透传 UUID: FFE0/FFE1
 * (与小程序 bleService.js 中默认 UUID 一致, 小程序端无需修改) */
#define BT24_BAUD        9600
#define BT24_RX_BUF_SIZE 128

void BT24_PinsSafe(void);                                             /* 上电先钳住PA2/PA3为安全电平 */
void USART2_Config(uint32_t baud);                                   /* 蓝牙串口初始化 */
void BT24_SendString(char *str);                                     /* 蓝牙发送字符串 */
void BT24_SendFrame(float temp, float humi, float gas,
                    uint8_t status, uint8_t fan, uint8_t alarm); /* 按协议发送JSON帧 (v2: +fan+alarm) */

extern volatile uint8_t  bt24_rx_done;   /* 收到一帧(空闲中断置1), 处理后手动清0 */
extern volatile uint16_t bt24_rx_len;    /* 本帧长度 */
extern char bt24_rx_buf[BT24_RX_BUF_SIZE]; /* 接收缓冲区 */

#endif
