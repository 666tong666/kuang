#include "mpuiic.h"
#include "Delay.h"

//MPU软件IIC引脚初始化 F4版本
void MPU_IIC_GPIO_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct;
	
	//F4 GPIO挂在AHB1，不是APB2
	RCC_AHB1PeriphClockCmd(SCL_RCC, ENABLE);
	
	//SCL
	GPIO_InitStruct.GPIO_Pin = SCL_PIN;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(SCL_GROUP, &GPIO_InitStruct);
	
	//SDA
	GPIO_InitStruct.GPIO_Pin = SDA_PIN;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(SDA_GROUP, &GPIO_InitStruct);
	
	IIC_SCL_SET(1);
	IIC_SDA_SET(1);
}

//IIC起始信号
void MPU_IIC_Start(void)
{
	IIC_SDA_SET(1);
	IIC_SCL_SET(1);
	delay_us(5);
	IIC_SDA_SET(0);
	delay_us(5);
	IIC_SCL_SET(0);
}

//IIC停止信号
void MPU_IIC_Stop(void)
{
	IIC_SDA_SET(0);
	IIC_SCL_SET(1);
	delay_us(5);
	IIC_SDA_SET(1);
	delay_us(5);
}

//等待应答
void MPU_IIC_WaitAck(void)
{
	uint8_t ack = 1;
	//SDA切换输入
	GPIO_InitTypeDef GPIO_InitStruct;
	GPIO_InitStruct.GPIO_Pin = SDA_PIN;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(SDA_GROUP, &GPIO_InitStruct);
	
	IIC_SCL_SET(1);
	delay_us(5);
	while(MPU_IIC_SDA_READ() == 0)
	{
		ack--;
		if(ack == 0) break;
	}
	IIC_SCL_SET(0);
	
	//切回推挽输出
	GPIO_InitStruct.GPIO_Pin = SDA_PIN;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(SDA_GROUP, &GPIO_InitStruct);
}

//发送应答  ack=0应答，ack=1非应答
void MPU_IIC_SendAck(uint8_t ack)
{
	if(ack)
		IIC_SDA_SET(1);
	else
		IIC_SDA_SET(0);
	delay_us(5);
	IIC_SCL_SET(1);
	delay_us(5);
	IIC_SCL_SET(0);
}

//写1字节
void MPU_IIC_WriteByte(uint8_t dat)
{
	uint8_t i;
	for(i=0;i<8;i++)
	{
		if(dat&0x80)
			IIC_SDA_SET(1);
		else
			IIC_SDA_SET(0);
		dat <<= 1;
		delay_us(2);
		IIC_SCL_SET(1);
		delay_us(2);
		IIC_SCL_SET(0);
	}
	MPU_IIC_WaitAck();
}

//读1字节，ack：0应答，1非应答
uint8_t MPU_IIC_ReadByte(uint8_t ack)
{
	uint8_t i,dat=0;
	GPIO_InitTypeDef GPIO_InitStruct;
	//SDA输入
	GPIO_InitStruct.GPIO_Pin = SDA_PIN;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(SDA_GROUP, &GPIO_InitStruct);
	
	for(i=0;i<8;i++)
	{
		dat <<=1;
		IIC_SCL_SET(1);
		delay_us(2);
		if(MPU_IIC_SDA_READ())
			dat |= 0x01;
		IIC_SCL_SET(0);
		delay_us(2);
	}
	
	//SDA切输出
	GPIO_InitStruct.GPIO_Pin = SDA_PIN;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(SDA_GROUP, &GPIO_InitStruct);
	
	MPU_IIC_SendAck(ack);
	return dat;
}