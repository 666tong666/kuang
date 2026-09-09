#include "TIM1.h"


void TIM1_Config(void)
{
	//TIM1_CH1  PA8   和 TIM1_CH1N  PB13
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
	
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
	
	GPIO_InitTypeDef mygpio={0};
	
	//引脚工作模式 : 复用模式
	mygpio.GPIO_Mode = GPIO_Mode_AF;
	//输出两种模式  推挽 PP   开漏  OD
	mygpio.GPIO_OType = GPIO_OType_PP;
	//引脚号
	mygpio.GPIO_Pin = TIM1_PIN;
	//输入的工作模式 
	mygpio.GPIO_PuPd = GPIO_PuPd_UP;
	//引脚的切换速度
	mygpio.GPIO_Speed = GPIO_High_Speed;
	
	GPIO_Init(TIM1_GROUP,&mygpio);
	
	//引脚号
	mygpio.GPIO_Pin = TIM1N_PIN;
	GPIO_Init(TIM1N_GROUP,&mygpio);
	
	
	//把PA8 和 PB13 复用成什么
	GPIO_PinAFConfig(GPIOA,GPIO_PinSource8,GPIO_AF_TIM1);
	GPIO_PinAFConfig(GPIOB,GPIO_PinSource13,GPIO_AF_TIM1);
	
	
	TIM_TimeBaseInitTypeDef mytim;
	
	//预分频 , 基本定时器选择不分频 (1倍分频)
	mytim.TIM_ClockDivision = TIM_CKD_DIV1; 
	mytim.TIM_CounterMode = TIM_CounterMode_Up;
	//T = (ARR+1)(PSC+1)/84000000    1
	//ARR
	mytim.TIM_Period = 999;    //8400
	//PSC
	mytim.TIM_Prescaler = 839; //10000
		
	TIM_TimeBaseInit(TIM1, &mytim);
	
	TIM_OCInitTypeDef myoc;
	
	//选择PWM模式1
	myoc.TIM_OCMode = TIM_OCMode_PWM1;
	//输出使能
	myoc.TIM_OutputState = TIM_OutputState_Enable;
	myoc.TIM_OutputNState = TIM_OutputNState_Enable;
	// CCR  默认蜂鸣器不响 
	myoc.TIM_Pulse = 0;
	//高电平有效
	myoc.TIM_OCPolarity = TIM_OCPolarity_High;
	myoc.TIM_OCNPolarity = TIM_OCNPolarity_High;
	
	//空闲时间 高电平还是低电平 
	myoc.TIM_OCIdleState = TIM_OCIdleState_Set;
	myoc.TIM_OCNIdleState = TIM_OCNIdleState_Reset;
	
	//   通道几 
	TIM_OC1Init(TIM1, &myoc);


	TIM_BDTRInitTypeDef mybdtr;
	//运行时 停止掉一个 pwm  依然保持互补波形
	mybdtr.TIM_OSSRState = TIM_OSSRState_Enable;
	//空闲时  pwm要不要取 64和 65行的状态
	mybdtr.TIM_OSSIState = TIM_OSSIState_Disable;
	/*
	  定时器锁级别
		定时器一旦运行了 
		#define TIM_LOCKLevel_OFF       不开启              
		#define TIM_LOCKLevel_1         不能修改OSSR OSSI  BRK刹车极性   只能修改死区时间            
		#define TIM_LOCKLevel_2         不能修改OSSR OSSI  BRK刹车极性   死区时间       
		#define TIM_LOCKLevel_3    			整个寄存器全部锁住  
	*/
	mybdtr.TIM_LOCKLevel = TIM_LOCKLevel_OFF;
	
	//死区时间  
	mybdtr.TIM_DeadTime = 3;
	//是否开启刹车  
	mybdtr.TIM_Break = TIM_Break_Enable;
	//选择刹车引脚是高电平还是低电平
	mybdtr.TIM_BreakPolarity = TIM_BreakPolarity_Low;
	//开启自动输出
	mybdtr.TIM_AutomaticOutput  = TIM_AutomaticOutput_Disable;
	
	TIM_BDTRConfig(TIM1, &mybdtr);
	
	//使能
	TIM_Cmd(TIM1,ENABLE);
	
	//开始输出PWM信号
	TIM_CtrlPWMOutputs(TIM1, ENABLE);

}


