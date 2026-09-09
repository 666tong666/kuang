#ifndef __IIC_H
#define __IIC_H

#include "stm32f4xx.h"
#include "Delay.h"
#include <stdio.h>



#define SCL_CLK RCC_AHB1Periph_GPIOB
#define SCL_GROUP GPIOB
#define SCL_PIN GPIO_Pin_8 

#define SDA_CLK RCC_AHB1Periph_GPIOB
#define SDA_GROUP GPIOB
#define SDA_PIN GPIO_Pin_9 

//�޸ĵ�ƽ״̬
#define IIC_SCL_SET(n) (n)?(GPIO_SetBits(SCL_GROUP,SCL_PIN)):(GPIO_ResetBits(SCL_GROUP,SCL_PIN))
#define IIC_SDA_SET(n) (n)?(GPIO_SetBits(SDA_GROUP,SDA_PIN)):(GPIO_ResetBits(SDA_GROUP,SDA_PIN))
#define IIC_SDA_READ GPIO_ReadInputDataBit(SDA_GROUP,SDA_PIN)


void IIC_SDA_Out(void);
void IIC_SDA_In(void);
void IIC_Config(void);
void IIC_Start(void);
void IIC_Stop(void);
void IIC_SendByte(uint8_t data);
uint8_t IIC_ReadByte(void);
uint8_t IIC_SlaveAck(void);
void IIC_MasterAck(uint8_t ack);

#endif