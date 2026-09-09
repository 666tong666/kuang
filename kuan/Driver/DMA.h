#ifndef __DMA_H
#define __DMA_H

#include "stm32f4xx.h"
#include "Delay.h"

void DMA_Config(uint32_t data_val, uint32_t memory_add);

void USART1_DMA_ENABLE(uint32_t data_val);


#endif