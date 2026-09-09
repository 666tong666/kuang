#include "TIM.h"
// TIM6 **** 和  TIM7
void TIM_Config(void)
{
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);
	
	
	TIM_TimeBaseInitTypeDef mytim;
	
	//预分频 , 基本定时器选择不分频 (1倍分频)
	mytim.TIM_ClockDivision = TIM_CKD_DIV1; 
	mytim.TIM_CounterMode = TIM_CounterMode_Up;
	//T = (ARR+1)(PSC+1)/84000000    1
	//0~65535      ARR 数多少个数 (自动重装载寄存器)
	mytim.TIM_Period = 9999;    //8400
	//0~65535      PSC 计数的快慢 (分频值)  
	mytim.TIM_Prescaler = 8399; //10000
	//高级定时器, 重复计数 
	//mytim.TIM_RepetitionCounter = 
		
	TIM_TimeBaseInit(TIM6, &mytim);
	
		
	NVIC_InitTypeDef mynvic={0};
	
	//中断通道   stm32f4xx.h
	mynvic.NVIC_IRQChannel = TIM6_DAC_IRQn;
	//使能
	mynvic.NVIC_IRQChannelCmd = ENABLE;
	//抢占优先级  0~15
	mynvic.NVIC_IRQChannelPreemptionPriority = 1;
	//响应优先级  0~15
	mynvic.NVIC_IRQChannelSubPriority = 1;
	
	NVIC_Init(&mynvic);
	//开启中断
	TIM_ITConfig(TIM6, TIM_IT_Update,ENABLE);
	
	TIM_Cmd(TIM6,ENABLE);
	
}

void TIM6_DAC_IRQHandler(void)
{
	if (TIM_GetITStatus(TIM6,TIM_IT_Update)) {
		//效果:LED1 1s闪烁一次   (main.c的代码清一清)
		GPIO_ToggleBits(LED_GROUP,LED1);
		IWDG_ReloadCounter(); //喂狗
		TIM_ClearITPendingBit(TIM6,TIM_IT_Update);
	}

}



/*
 
 1s 定时  

 T = PSC  和  ARR有关系 

 STM32F407    84MHz   -> 1s 能数 84000000 个数    1个数   1/84000000

 1. 新的时钟的频率  1s    84000000Hz / (PSC+1)   个数      
 2. 1个数     (PSC+1)/84000000  s 
 3. T = (ARR+1)(PSC+1)/84000000


 PSC    较大   适合  长时间的定时器  
 ARR    较大   短时间  高精度的定时 

  
*/
