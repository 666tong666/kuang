#ifndef __DAC_H
#define __DAC_H

#include "stm32f4xx.h"
#include "Delay.h"
#include <stdio.h>



#define DAC_GROUP GPIOA
#define DAC_PIN GPIO_Pin_5 



void DAC_Config(void);



#endif