#include "KEY.h"
//1. 按键 引脚 PG2~PG5   2. 工作模式: 输入     3. 按下 低 

void KEY_Config(void)
{
	//使能GPIO时钟
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOG, ENABLE);
	
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

}



