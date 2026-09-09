/**
 * @file    fan.c
 * @brief   散热风扇驱动实现（仅开关，不调速）
 */
#include "fan.h"

/**
 * @brief  初始化风扇 GPIO（推挽输出，默认关闭）
 */
void Fan_Init(void)
{
    GPIO_InitTypeDef gpio;

    /* 1. 开启 GPIOF 时钟 */
    RCC_AHB1PeriphClockCmd(FAN_GPIO_CLK, ENABLE);

    /* 2. 配置 PF6 为推挽输出、默认低电平（风扇关闭） */
    gpio.GPIO_Pin   = FAN_GPIO_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_OUT;
    gpio.GPIO_OType = GPIO_OType_PP;     /* 推挽输出 */
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init(FAN_GPIO_PORT, &gpio);

    GPIO_ResetBits(FAN_GPIO_PORT, FAN_GPIO_PIN);
}

/**
 * @brief  开启风扇（PF6 输出高）
 */
void Fan_On(void)
{
    GPIO_SetBits(FAN_GPIO_PORT, FAN_GPIO_PIN);
}

/**
 * @brief  关闭风扇（PF6 输出低）
 */
void Fan_Off(void)
{
    GPIO_ResetBits(FAN_GPIO_PORT, FAN_GPIO_PIN);
}

/**
 * @brief  设置风扇开关
 */
void Fan_Set(uint8_t on)
{
    if (on) Fan_On();
    else    Fan_Off();
}

/**
 * @brief  读取当前风扇状态
 */
uint8_t Fan_IsOn(void)
{
    return (GPIO_ReadOutputDataBit(FAN_GPIO_PORT, FAN_GPIO_PIN) == FAN_ON_LEVEL) ? 1u : 0u;
}