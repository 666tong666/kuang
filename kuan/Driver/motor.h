#ifndef __MOTOR_H
#define __MOTOR_H

#include "stm32f4xx.h"

//INA PWM PB6 TIM4_CH1
#define MOTOR_INA_PIN    GPIO_Pin_6
#define MOTOR_INA_PORT   GPIOB
//INB ∆’Õ®IO PE6
#define MOTOR_INB_PIN    GPIO_Pin_6
#define MOTOR_INB_PORT   GPIOE

void Motor_Init(void);
void Motor_Forward(uint16_t pwm_val);
void Motor_Backward(uint16_t pwm_val);
void Motor_Stop(void);
void Motor_Brake(void);

#endif