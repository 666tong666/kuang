#include "motor.h"

void Motor_Init(void)
{
    GPIO_InitTypeDef        GPIO_InitStruct;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStruct;
    TIM_OCInitTypeDef       TIM_OCInitStruct;

    //开启时钟
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB | RCC_AHB1Periph_GPIOE, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);

    //PE6 INB 普通推挽输出
    GPIO_InitStruct.GPIO_Pin = MOTOR_INB_PIN;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(MOTOR_INB_PORT, &GPIO_InitStruct);

    //PB6 TIM4_CH1 复用PWM
    GPIO_InitStruct.GPIO_Pin = MOTOR_INA_PIN;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(MOTOR_INA_PORT, &GPIO_InitStruct);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource6, GPIO_AF_TIM4);

    //TIM4 APB1定时器时钟84M，预分频83，ARR=999 →PWM 1KHz，占空比0?999
    TIM_TimeBaseStruct.TIM_Prescaler = 83;
    TIM_TimeBaseStruct.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseStruct.TIM_Period = 999;
    TIM_TimeBaseStruct.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM4, &TIM_TimeBaseStruct);

    TIM_OCInitStruct.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStruct.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStruct.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(TIM4, &TIM_OCInitStruct);

    TIM_Cmd(TIM4, ENABLE);
}

//正转：INA输出PWM，INB=0
void Motor_Forward(uint16_t pwm_val)
{
    GPIO_WriteBit(MOTOR_INB_PORT, MOTOR_INB_PIN, Bit_RESET);
    TIM_SetCompare1(TIM4, pwm_val);
}

//反转：INA=0，INB=1
void Motor_Backward(uint16_t pwm_val)
{
    GPIO_WriteBit(MOTOR_INB_PORT, MOTOR_INB_PIN, Bit_SET);
    TIM_SetCompare1(TIM4, 0);
}

//停止 INA=0 INB=0
void Motor_Stop(void)
{
    GPIO_WriteBit(MOTOR_INB_PORT, MOTOR_INB_PIN, Bit_RESET);
    TIM_SetCompare1(TIM4, 0);
}

//刹车急停 INA=1 INB=1
void Motor_Brake(void)
{
    GPIO_WriteBit(MOTOR_INB_PORT, MOTOR_INB_PIN, Bit_SET);
    TIM_SetCompare1(TIM4, 999);
}