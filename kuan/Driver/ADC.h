#ifndef __ADC_H
#define __ADC_H

#include "stm32f4xx.h"
#include "Delay.h"
#include <stdio.h>
#include "light_adc.h"


#define ADC_GROUP GPIOF
#define ADC_PIN GPIO_Pin_7 



void ADC_Config(void);
uint16_t ADC_GetData(void);
float ADC_Get_Ave(uint8_t times);
uint16_t ADC_Get_Light(uint8_t times);


#endif