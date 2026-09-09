#ifndef __MQ135_H
#define __MQ135_H

#include "stm32f4xx.h"

void ADC_MQ135_Init(void);
uint16_t MQ135_Get_ADC(void);
uint16_t MQ135_Get_ADC_Avg(uint8_t n);   /* n次采样取平均, 抗随机噪声 */

#endif