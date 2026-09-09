#include "Delay.h"

// uint8_t  uint16_t  uint32_t   
// 0~255     0~65535  0~xxxxxx

void Delay(volatile uint32_t cnt)
{

	while (cnt > 0){
		cnt--;
	}
}

// 1s = 1000ms   1ms = 1000us 

// 21MHz -> 1us 数 21 个数

void delay_us(uint32_t us)
{
	//  000000000 000000000 000000000 000000000
	SysTick->CTRL = 0;
	// 设置重装载值
	SysTick->LOAD = us * 21 ;
	//清空一下当前计数寄存器
	SysTick->VAL = 0;
	//使能
	SysTick->CTRL = 1;
	//卡住 卡到数完 
	//0111 1000  如何判断 第四位是不是 1 ?????
	//0000 1000  按位与
	//----------------------------------
	//0000 1000  == 0
	
	while((SysTick->CTRL & 0x00010000) == 0);
	//关闭定时器
	SysTick->CTRL = 0;	
}

// ms 数值不能超过 798 
void delay_ms(uint32_t ms)
{
	//  000000000 000000000 000000000 000000000
	SysTick->CTRL = 0;
	// 设置重装载值
	SysTick->LOAD = ms * 21 * 1000 ;
	//清空一下当前计数寄存器
	SysTick->VAL = 0;
	//使能
	SysTick->CTRL = 1;
	//卡住 卡到数完 
	//0111 1000  如何判断 第四位是不是 1 ?????
	//0000 1000  按位与
	//----------------------------------
	//0000 1000  == 0
	
	while((SysTick->CTRL & 0x00010000) == 0);
	//关闭定时器
	SysTick->CTRL = 0;	

}


void delay_s(uint32_t s)
{	
	while(s--)
	{
		delay_ms(500);
		delay_ms(500);
	}

}
