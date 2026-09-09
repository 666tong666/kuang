#include "EXTI.h"

void EXTI_Config(void)
{
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOG, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
	
	GPIO_InitTypeDef mygpio;
	
	//引脚工作模式 : 输出模式
	mygpio.GPIO_Mode = GPIO_Mode_IN;
	//输出两种模式  推挽 PP   开漏  OD
	mygpio.GPIO_OType = GPIO_OType_PP;
	//引脚号
	mygpio.GPIO_Pin = KEY0 | KEY1 | KEY2 | KEY3;
	//输入的工作模式 
	mygpio.GPIO_PuPd = GPIO_PuPd_UP;
	//引脚的切换速度
	mygpio.GPIO_Speed = GPIO_High_Speed;
	
	GPIO_Init(KEY_GROUP,&mygpio);

	//把引脚 和 中断线 相连
	SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOG,EXTI_PinSource2);
	SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOG,EXTI_PinSource3);
	// .... 
	
	
	EXTI_InitTypeDef myexit;
	
	// 哪几根中断线
	myexit.EXTI_Line = EXTI_Line2 | EXTI_Line3;
	//使能
	myexit.EXTI_LineCmd = ENABLE;
	//本次工作模式 中断 / 事件  
	myexit.EXTI_Mode = EXTI_Mode_Interrupt;
	//触发条件: 下降沿
	myexit.EXTI_Trigger = EXTI_Trigger_Falling;
	
	EXTI_Init(&myexit);
	
	
	NVIC_InitTypeDef mynvic={0};
	
	//中断通道
	mynvic.NVIC_IRQChannel = EXTI2_IRQn;
	//使能
	mynvic.NVIC_IRQChannelCmd = ENABLE;
	//抢占优先级  0~15
	mynvic.NVIC_IRQChannelPreemptionPriority = 1;
	//响应优先级  0~15
	mynvic.NVIC_IRQChannelSubPriority = 1;
	
	NVIC_Init(&mynvic);
	
	
	//中断通道
	mynvic.NVIC_IRQChannel = EXTI3_IRQn;
	//使能
	mynvic.NVIC_IRQChannelCmd = ENABLE;
	//抢占优先级  0~15
	mynvic.NVIC_IRQChannelPreemptionPriority = 1;
	//响应优先级  0~15
	mynvic.NVIC_IRQChannelSubPriority = 1;
	
	NVIC_Init(&mynvic);
	
}

//中断函数   触发中断的时候  自动调用的函数  中断函数  ---- startup_stmxxxxx
uint8_t key1_flag = 0;

void EXTI2_IRQHandler(void)
{
	//严谨 ---- 进来的是不是的2号中断     PA2  PG2 
	if (EXTI_GetITStatus(EXTI_Line2) == SET) {
		
		//if (GPIO_ReadInputDataBit(KEY_GROUP,KEY0) == 0) {
			//GPIO_ToggleBits(LED_GROUP,LED1);
			key1_flag = 1;
			//清除中断 函数怎么写  
			EXTI_ClearITPendingBit(EXTI_Line2);
		//}
	}
}

