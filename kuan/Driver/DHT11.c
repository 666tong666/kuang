#include "DHT11.h"

uint8_t humi_int = 0;
uint8_t humi_deci = 0;
uint8_t temp_int = 0;
uint8_t temp_deci = 0;
uint8_t check_sum = 0;
uint8_t dht_err = 1;

void DHT11_Output_Mode(void)
{
	RCC_AHB1PeriphClockCmd(DHT11_CLK, ENABLE);
	GPIO_InitTypeDef mygpio;
	mygpio.GPIO_Mode = GPIO_Mode_OUT;
	mygpio.GPIO_OType = GPIO_OType_PP;
	mygpio.GPIO_Pin = DHT11_PIN;
	mygpio.GPIO_PuPd = GPIO_PuPd_UP;
	mygpio.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_Init(DHT11_GROUP, &mygpio);
}

void DHT11_Input_Mode(void)
{
	GPIO_InitTypeDef mygpio;
	mygpio.GPIO_Mode = GPIO_Mode_IN;
	mygpio.GPIO_Pin = DHT11_PIN;
	mygpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(DHT11_GROUP, &mygpio);
}

void DHT11_Set(uint8_t n)
{
	if(n == 1)
		GPIO_SetBits(DHT11_GROUP, DHT11_PIN);
	else
		GPIO_ResetBits(DHT11_GROUP, DHT11_PIN);
}

uint8_t DHT11_Read(void)
{
	return GPIO_ReadInputDataBit(DHT11_GROUP, DHT11_PIN);
}

void DHT11_Start(void)
{
	DHT11_Output_Mode();
	DHT11_Set(0);
	delay_ms(20);
	DHT11_Set(1);
	delay_us(30);
}

uint8_t DHT11_Back(void)
{
	uint16_t count;
	DHT11_Input_Mode();
	if(DHT11_Read() == 0)
	{
		count = 0;
		while(DHT11_Read() == 0)
		{
			count++;
			if(count > 9)	return 1;
			delay_us(10);
		}
		count = 0;
		while(DHT11_Read() == 1)
		{
			count++;
			if(count > 9)	return 2;
			delay_us(10);
		}
		return 0;
	}
	return 3;
}

uint8_t DHT11_Get(void)
{
	uint8_t temp = 0;
	for(uint8_t i = 0; i < 8; i++)
	{
		while(DHT11_Read() == 0);
		delay_us(35);
		if(DHT11_Read() == 1)
		{
			while(DHT11_Read() == 1);
			temp |= 0x01 << (7 - i);
		}
	}
	return temp;
}

void DHT11_Read_Data(void)
{
	DHT11_Start();
	if(DHT11_Back() == 0)
	{
		humi_int 	= DHT11_Get();
		humi_deci 	= DHT11_Get();
		temp_int 	= DHT11_Get();
		temp_deci 	= DHT11_Get();
		check_sum 	= DHT11_Get();

		DHT11_Output_Mode();
		DHT11_Set(1);

		if(check_sum == (humi_int + humi_deci + temp_int + temp_deci))
		{
			dht_err = 0;
		}
		else
		{
			dht_err = 1;
		}
	}
	else
	{
		dht_err = 1;
	}
	delay_ms(200);
}
