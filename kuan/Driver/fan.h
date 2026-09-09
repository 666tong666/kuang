/**
 * @file    fan.h
 * @brief   散热风扇驱动（仅开关，不调速）
 * @note    硬件: PF6 推挽输出，驱动 N-MOS / 风扇模块
 *          - 高电平 = 风扇运行
 *          - 低电平 = 风扇关闭
 * @version 1.0
 * @date    2026-09-09
 */
#ifndef __FAN_H
#define __FAN_H

#include "stm32f4xx.h"

/* ====================== 硬件配置 ====================== */
#define FAN_GPIO_PORT      GPIOF
#define FAN_GPIO_CLK       RCC_AHB1Periph_GPIOF
#define FAN_GPIO_PIN       GPIO_Pin_6
#define FAN_GPIO_PINSOURCE GPIO_PinSource6
#define FAN_GPIO_FUNCTION  GPIO_Function_0   /* 占位：仅示意 */

#define FAN_ON_LEVEL       1   /* PF6 输出高 = 风扇运行 */
#define FAN_OFF_LEVEL      0

/* ====================== API ====================== */

/**
 * @brief  初始化风扇 GPIO（默认关闭）
 */
void Fan_Init(void);

/**
 * @brief  开启风扇
 */
void Fan_On(void);

/**
 * @brief  关闭风扇
 */
void Fan_Off(void);

/**
 * @brief  设置风扇开关
 * @param  on 1=开启 0=关闭
 */
void Fan_Set(uint8_t on);

/**
 * @brief  读取当前风扇状态
 * @return 1=运行中 0=关闭
 */
uint8_t Fan_IsOn(void);

#endif /* __FAN_H */