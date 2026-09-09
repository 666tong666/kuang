#include "Cap.h"


void CAP_Config(void)
{
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
	
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
	
	GPIO_InitTypeDef mygpio={0};
	
	//引脚工作模式 : 复用模式
	mygpio.GPIO_Mode = GPIO_Mode_AF;
	//输出两种模式  推挽 PP   开漏  OD
	mygpio.GPIO_OType = GPIO_OType_PP;
	//引脚号
	mygpio.GPIO_Pin = CAP_PIN;
	//输入的工作模式 
	mygpio.GPIO_PuPd = GPIO_PuPd_UP;
	//引脚的切换速度
	mygpio.GPIO_Speed = GPIO_High_Speed;
	
	GPIO_Init(CAP_GROUP,&mygpio);
	
	//把PA9 和 PA10 复用成什么
	GPIO_PinAFConfig(GPIOC,GPIO_PinSource7,GPIO_AF_TIM3);
	
	
	TIM_TimeBaseInitTypeDef mytim;
	
	//预分频 , 基本定时器选择不分频 (1倍分频)
	mytim.TIM_ClockDivision = TIM_CKD_DIV1; 
	mytim.TIM_CounterMode = TIM_CounterMode_Up;
	//T = (ARR+1)(PSC+1)/84000000    1
	//ARR
	mytim.TIM_Period = 999;    //8400
	//PSC  
	mytim.TIM_Prescaler = 839; //10000       1s   100000个数    1/100000    84Mhz / PSC
		
	TIM_TimeBaseInit(TIM3, &mytim);
	
	TIM_ICInitTypeDef mycap;
	
	//选择定时器通道
	mycap.TIM_Channel = TIM_Channel_2;
	//滤波系数   数字越大滤波强度越高     1~3 轻度   4~7 中度  强滤波 
	mycap.TIM_ICFilter = 0x0;
	//检测的边沿  起手上升 , 过一会检测下降 ----- 高电平的时间 
	mycap.TIM_ICPolarity = TIM_ICPolarity_Rising;
	//捕获分频
	mycap.TIM_ICPrescaler = TIM_ICPSC_DIV1;
	//选择触发通道
	mycap.TIM_ICSelection = TIM_ICSelection_DirectTI;
	
	TIM_ICInit(TIM3, &mycap);
	
		NVIC_InitTypeDef mynvic={0};
	
	//中断通道
	mynvic.NVIC_IRQChannel = TIM3_IRQn;
	//使能
	mynvic.NVIC_IRQChannelCmd = ENABLE;
	//抢占优先级  0~15
	mynvic.NVIC_IRQChannelPreemptionPriority = 1;
	//响应优先级  0~15
	mynvic.NVIC_IRQChannelSubPriority = 1;
	
	NVIC_Init(&mynvic);
	
	//同时开启两个中断  1. 通道2上面的输入捕获中断   2. 计时器计时结束中断
	TIM_ITConfig(TIM3, TIM_IT_CC2 | TIM_IT_Update,ENABLE);
	
	//使能
	TIM_Cmd(TIM3,DISABLE);
	
}

//计算时间   进来的 信号 的 高电平持续的时间  

uint32_t cnt = 0;   //装定时器 数了几个 0.1
uint8_t flag = 1;   //指定当前是第几次进来    奇数:上升   偶数:下降 
uint32_t cap_v1 = 0;
uint32_t cap_v2 = 0;
uint32_t t = 0;   //真正的时间

void TIM3_IRQHandler(void)
{
	//捕获引脚边沿变化中断
	if (TIM_GetITStatus(TIM3,TIM_IT_CC2) == SET){
		//边沿变化触发
		
		if(flag == 1) {
			cnt = 0;
			flag = 0;
			//获得这一刻  定时器 数到几
			cap_v1 = TIM_GetCapture2(TIM3);  //~~~~~~~~~ 1
			
			// 修改 下一次捕获 下降沿 
			TIM_OC2PolarityConfig(TIM3,TIM_OCPolarity_Low);
			
		} else {
			flag = 1;
			//获得这一刻  定时器 数到几
			cap_v2 = TIM_GetCapture2(TIM3);   //~~~~~~~~~~~~~~~~~2
			// 1000 是从 定时器配置的 ARR 那里来的
			t = (cnt * 1000 - cap_v1 + cap_v2) *  (1/100000);     //~~~~~~~~~~~~3  
			
			//t * 1个数消耗的时间     * 速度   / 2  
			
			//修改下一次捕获  上升 
			TIM_OC2PolarityConfig(TIM3,TIM_OCPolarity_High);
		
		}
		TIM_ClearITPendingBit(TIM3,TIM_IT_CC2);
		
	}
	
	//计时结束的中断
	if (TIM_GetITStatus(TIM3,TIM_IT_Update) == SET){
		//0.01s  触发一次 
		cnt++;
		TIM_ClearITPendingBit(TIM3,TIM_IT_Update);
		
	}
	
}


