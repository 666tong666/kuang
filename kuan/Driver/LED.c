#include "LED.h"

//GPIO配置  点灯 
//1. 引脚号 PG14 PG13 PG11 PA6
//2. 工作模式 : 推挽输出    
//3. 低电平 点亮      
// .c       .h

void LED_Config(void)
{
	//使能GPIO时钟
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOG, ENABLE);
	
	GPIO_InitTypeDef mygpio;
	
	//引脚工作模式 : 输出模式
	mygpio.GPIO_Mode = GPIO_Mode_OUT;
	//输出两种模式  推挽 PP   开漏  OD
	mygpio.GPIO_OType = GPIO_OType_PP;
	//引脚号
	mygpio.GPIO_Pin = GPIO_Pin_11 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_6;
	//输入的工作模式 
	//mygpio.GPIO_PuPd = 
	//引脚的切换速度
	mygpio.GPIO_Speed = GPIO_High_Speed;
	
	GPIO_Init(GPIOG,&mygpio);
	
	//GPIO_SetBits()   高电平     /GPIO_ResetBits()   低电平
	//配置完 默认所有灯都熄灭
	GPIO_SetBits(GPIOG,GPIO_Pin_11 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_6);
}


void LED_Horse(uint32_t ms)
{
	uint16_t leds[] = {LED1,LED2,LED3,LED4};
	
	for (int i = 0; i < 4; i++) {
		GPIO_ResetBits(LED_GROUP,leds[i]);
		delay_ms(ms);
		GPIO_SetBits(LED_GROUP,leds[i]);
	}
}


void LED_Water(uint32_t ms)
{
	uint16_t leds[] = {LED1,LED2,LED3,LED4};
	
	for (int i = 0; i < 4; i++) {
		
		delay_ms(ms);
		GPIO_ResetBits(LED_GROUP,leds[i]);
	
	}

	for (int i = 0; i < 4; i++) {
		
		delay_ms(400);
		GPIO_SetBits(LED_GROUP,leds[i]);
	
	}
	
}

//点灯的模式切换
void LED_Mode(uint8_t flag,uint32_t ms)
{
	if(flag == 0) {
		LED_Horse(ms);
	} else {
		LED_Water(ms);
	}

}



