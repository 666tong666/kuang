#ifndef __EXTI_H
#define __EXTI_H

#include "stm32f4xx.h"
#include "KEY.h"
#include "LED.h"

void EXTI_Config(void);

extern uint8_t key1_flag;


#endif
