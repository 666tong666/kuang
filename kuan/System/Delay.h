#ifndef __DELAY_H
#define __DELAY_H

#include "stm32f4xx.h"

void DWT_Init(void);                 /* 上电先调用一次: 使能 DWT 周期计数器 */
void Delay(volatile uint32_t cnt);
void delay_us(uint32_t us);          /* us 级忙等, 中断/临界区内可用 */
void delay_ms(uint32_t ms);          /* ms 级忙等, 任务内长延时请用 vTaskDelay */
void delay_s(uint32_t s);

#endif
