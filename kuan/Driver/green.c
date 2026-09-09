#include "stm32f4xx.h"
#include "green.h"

#define LED_PIN     GPIO_Pin_3
#define LED_PORT    GPIOB
#define LED_RCC     RCC_AHB1Periph_GPIOB

#define LED_PINR     GPIO_Pin_6
#define LED_PORTR    GPIOD
#define LED_RCCR     RCC_AHB1Periph_GPIOD

void LED_Init(void)
{
	//=========ÂÌµÆ======================
	GPIO_InitTypeDef GPIO_InitStruct;
	
	RCC_AHB1PeriphClockCmd(LED_RCC, ENABLE);
	
	GPIO_InitStruct.GPIO_Pin = LED_PIN;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(LED_PORT, &GPIO_InitStruct);
	
	GPIO_ResetBits(LED_PORTR, LED_PINR); // Ä¬ÈÏÏ¨Ãð
	
	
	//=========ºìµÆ======================
	GPIO_InitTypeDef GPIO_InitStructs;
	
	RCC_AHB1PeriphClockCmd(LED_RCCR, ENABLE);
	
	GPIO_InitStructs.GPIO_Pin = LED_PINR;
	GPIO_InitStructs.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructs.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructs.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructs.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(LED_PORTR, &GPIO_InitStructs);
	
	GPIO_ResetBits(LED_PORTR, LED_PINR); // Ä¬ÈÏÏ¨Ãð
}

//µãÁÁPB3
void LED_On(void)
{
	GPIO_SetBits(LED_PORT, LED_PIN);
	GPIO_ResetBits(LED_PORTR, LED_PINR);

}

//Ï¨ÃðPB3
void LED_Off(void)
{
	GPIO_ResetBits(LED_PORT, LED_PIN);
	GPIO_SetBits(LED_PORTR, LED_PINR);

}

//·­×ªµçÆ½
void LED_Toggle(void)
{
	if(GPIO_ReadOutputDataBit(LED_PORT,LED_PIN))
		GPIO_ResetBits(LED_PORT,LED_PIN);
	else
		GPIO_SetBits(LED_PORT,LED_PIN);
}