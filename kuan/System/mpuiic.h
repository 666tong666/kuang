#ifndef __MPUIIC_H
#define __MPUIIC_H
#include "stm32f4xx.h"
#include "Delay.h"

//========引脚配置 PC7?SCL  PC8?SDA========
#define SCL_GROUP        GPIOB
#define SCL_PIN          GPIO_Pin_7
#define SCL_RCC          RCC_AHB1Periph_GPIOB

#define SDA_GROUP        GPIOB
#define SDA_PIN          GPIO_Pin_8
#define SDA_RCC          RCC_AHB1Periph_GPIOB

//F4标准库：Bit_SET  Bit_RESET，不要写BitSet/BitReset
#define IIC_SCL_SET(x)    GPIO_WriteBit(SCL_GROUP,SCL_PIN,(x)?Bit_SET:Bit_RESET)
#define IIC_SDA_SET(x)    GPIO_WriteBit(SDA_GROUP,SDA_PIN,(x)?Bit_SET:Bit_RESET)
#define MPU_IIC_SDA_READ()  GPIO_ReadInputDataBit(SDA_GROUP,SDA_PIN)

void MPU_IIC_GPIO_Init(void);
void MPU_IIC_Start(void);
void MPU_IIC_Stop(void);
void MPU_IIC_WaitAck(void);
void MPU_IIC_SendAck(uint8_t ack);
void MPU_IIC_WriteByte(uint8_t dat);
uint8_t MPU_IIC_ReadByte(uint8_t ack);

#endif
