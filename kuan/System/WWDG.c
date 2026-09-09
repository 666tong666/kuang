#include "WWDG.h"

void WWDG_Config(void)
{
	//开启时钟
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_WWDG, ENABLE);
	
	// 预分频
	WWDG_SetPrescaler(WWDG_Prescaler_2);
	
	//指定一个窗口值  大于63  小于127
	WWDG_SetWindowValue(80);
	
	//使能看门狗 , 并设置一个重装载值
  WWDG_Enable(127);
	
	//窗口看门狗 特殊的功能  ----- 极限喂狗 
	
	
	NVIC_InitTypeDef mynvic={0};
	
	//中断通道
	mynvic.NVIC_IRQChannel = WWDG_IRQn;
	//使能
	mynvic.NVIC_IRQChannelCmd = ENABLE;
	//抢占优先级  0~15
	mynvic.NVIC_IRQChannelPreemptionPriority = 0;
	//响应优先级  0~15
	mynvic.NVIC_IRQChannelSubPriority = 1;
	
	NVIC_Init(&mynvic);
	
	
	//清除所有标志位  防止收到上一次计数的影响
	WWDG_ClearFlag();
	
	//使能中断
	WWDG_EnableIT();
}

//极限喂狗中断
void WWDG_IRQHandler(void)
{

	if (WWDG_GetFlagStatus() == SET)
	{
		WWDG_SetCounter(127);  //喂狗
		WWDG_ClearFlag();
	}
}
