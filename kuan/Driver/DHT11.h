#ifndef __DHT11_H
#define __DHT11_H

#include "stm32f4xx.h"
#include "Delay.h"

#define DHT11_CLK 		RCC_AHB1Periph_GPIOA
#define DHT11_GROUP 	GPIOA
#define DHT11_PIN 		GPIO_Pin_5

extern uint8_t dht_err;

void DHT11_Output_Mode(void);
void DHT11_Input_Mode(void);
void DHT11_Set(uint8_t n);
uint8_t DHT11_Read(void);
void DHT11_Start(void);
uint8_t DHT11_Back(void);
uint8_t DHT11_Get(void);
void DHT11_Read_Data(void);

extern uint8_t humi_int;
extern uint8_t humi_deci;
extern uint8_t temp_int;
extern uint8_t temp_deci;
extern uint8_t check_sum;

#endif
