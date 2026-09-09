#include "stm32f4xx.h"
#include "Delay.h"

// 引脚定义 PB8
#define BEEP_PIN    GPIO_Pin_9
#define BEEP_PORT   GPIOB
#define BEEP_RCC    RCC_AHB1Periph_GPIOB

void BEEP_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct;
	
	RCC_AHB1PeriphClockCmd(BEEP_RCC, ENABLE);
	
	GPIO_InitStruct.GPIO_Pin = BEEP_PIN;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;  //推挽输出
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(BEEP_PORT, &GPIO_InitStruct);
	
	GPIO_SetBits(BEEP_PORT,BEEP_PIN); // 默认关闭蜂鸣器
}

//打开蜂鸣器
void BEEP_On(void)
{
	GPIO_ResetBits(BEEP_PORT,BEEP_PIN);
}

//关闭蜂鸣器
void BEEP_Off(void)
{
	GPIO_SetBits(BEEP_PORT,BEEP_PIN);
}

//鸣叫一段时间，阻塞
void BEEP_Beep(uint32_t ms)
{
	BEEP_On();
	delay_ms(ms);
	BEEP_Off();
}