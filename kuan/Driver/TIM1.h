#ifndef __TIM1_H
#define __TIM1_H

#include "stm32f4xx.h"
#include "LED.h"


#define TIM1_GROUP GPIOA
#define TIM1_PIN  GPIO_Pin_8

#define TIM1N_GROUP GPIOB
#define TIM1N_PIN  GPIO_Pin_13


void TIM1_Config(void);


#endif