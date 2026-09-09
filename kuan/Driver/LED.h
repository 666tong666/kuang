#ifndef __LED_H
#define __LED_H

#include "stm32f4xx.h"
#include "Delay.h"



#define LED_GROUP GPIOG
#define LED1 GPIO_Pin_14 
#define LED2 GPIO_Pin_13 
#define LED3 GPIO_Pin_6 
#define LED4 GPIO_Pin_11


void LED_Config(void);
void LED_Horse(uint32_t ms);
void LED_Water(uint32_t ms);
void LED_Mode(uint8_t flag,uint32_t ms);



#endif