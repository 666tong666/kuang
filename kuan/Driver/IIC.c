#include "IIC.h"

//硬件IIC 和 软件IIC(本次)


//配置 输出模式
void IIC_SDA_Out(void)
{
	RCC_AHB1PeriphClockCmd(SDA_CLK, ENABLE);
	
	GPIO_InitTypeDef mygpio;
	
	//引脚工作模式 : 输出模式
	mygpio.GPIO_Mode = GPIO_Mode_OUT;
	//输出两种模式  推挽 PP   开漏  OD
	mygpio.GPIO_OType = GPIO_OType_OD;
	//引脚号
	mygpio.GPIO_Pin = SDA_PIN;
	//输入的工作模式 
	mygpio.GPIO_PuPd = GPIO_PuPd_UP;
	//引脚的切换速度
	mygpio.GPIO_Speed = GPIO_High_Speed;
	
	GPIO_Init(SDA_GROUP,&mygpio);
}



void IIC_SDA_In(void)
{
	//一定是先有输出  后有输入 , 时钟 只需要 打开一次
	GPIO_InitTypeDef mygpio;
	
	//引脚工作模式 : 输出模式
	mygpio.GPIO_Mode = GPIO_Mode_IN;
	//引脚号
	mygpio.GPIO_Pin = SDA_PIN;
	//输入的工作模式   完全让DHT11总线 控制电平
	mygpio.GPIO_PuPd = GPIO_PuPd_UP;
	
	GPIO_Init(SDA_GROUP,&mygpio);
}


//IIC两条线初始的电平  SCL 和 SDA 输出 都高
void IIC_Config(void)
{
	
	RCC_AHB1PeriphClockCmd(SCL_CLK, ENABLE);
	
	GPIO_InitTypeDef mygpio;
	
	//引脚工作模式 : 输出模式
	mygpio.GPIO_Mode = GPIO_Mode_OUT;
	//输出两种模式  推挽 PP   开漏  OD
	mygpio.GPIO_OType = GPIO_OType_OD;
	//引脚号
	mygpio.GPIO_Pin = SCL_PIN;
	//输入的工作模式 
	mygpio.GPIO_PuPd = GPIO_PuPd_UP;
	//引脚的切换速度
	mygpio.GPIO_Speed = GPIO_High_Speed;
	
	GPIO_Init(SCL_GROUP,&mygpio);
	
	mygpio.GPIO_Pin = SDA_PIN;
	GPIO_Init(SDA_GROUP,&mygpio);
	
	//都高电平
	IIC_SCL_SET(1);
	IIC_SDA_SET(1);
}

void IIC_Start(void)
{
	//sda输出
	IIC_SDA_Out();
	//都拉高
	IIC_SCL_SET(1);
	IIC_SDA_SET(1);
	//几微秒  1~10
	delay_us(5);
	
	//先把SDA拉低
	IIC_SDA_SET(0);
	delay_us(5);
	//再把SCL拉低
	IIC_SCL_SET(0);
	delay_us(5);

}

//结束信号
void IIC_Stop(void)
{
	//sda输出
	IIC_SDA_Out();
	IIC_SCL_SET(0);
	IIC_SDA_SET(0);
	//几微秒  1~10
	delay_us(5);
	
	IIC_SCL_SET(1);
	delay_us(5);
	
	IIC_SDA_SET(1);
	delay_us(5);

}

//发送 数据  a  0110 0111
void IIC_SendByte(uint8_t data)
{
	
	IIC_SDA_Out();
	
	IIC_SCL_SET(0);
	IIC_SDA_SET(0);
	
	//一次发送 8bit 
	for (uint8_t i = 0; i < 8; i++) {
		
		//假设 data = 01100111    & 10000000 = 00000000
		if (data & 0x80) {
			//高
			IIC_SDA_SET(1);
		
		} else {
			//低
			IIC_SDA_SET(0);
		}
		delay_us(5);
		data <<= 1;  //data数据左移一位 , 每次发送的都是最高位
		
		
		IIC_SCL_SET(1);
		delay_us(5);
		
		
		IIC_SCL_SET(0);
		delay_us(5);
	}
		
}

//读取数据
uint8_t IIC_ReadByte(void)
{
	uint8_t data = 0;
	//切换成输入模式
	IIC_SDA_In();
	//只拉低 SCL , 因SDA是由外设控制的了
	IIC_SCL_SET(0);
	delay_us(5);
	
	for (uint8_t i = 0; i < 8; i++)
	{
		// 把时钟线拉高
		IIC_SCL_SET(1);
		delay_us(5);

		data <<= 1;
		if (IIC_SDA_READ) {
			//高 永远都新的到的内容 放在最低位就可以 
			data |= 0x01;
		}
		//没写else  0 不用处理
		
		IIC_SCL_SET(0);
		delay_us(5);
		
	}
	return data;
}


//从机应答  因为主机 -> 从 发信息了   从 -> 告诉主机 我是不是收到了

uint8_t IIC_SlaveAck(void)
{
	
	uint8_t ack = 0;
	//切换成输入模式
	IIC_SDA_In();
	//拉低时钟线 , 等待从机切换 SDA引脚的应答状态
	IIC_SCL_SET(0);
	delay_us(5);
	
	IIC_SCL_SET(1);
	delay_us(5);
	
	if (IIC_SDA_READ == 0) 
		ack = 0;  //应答
	else 
		ack = 1;  //不答应
	
	IIC_SCL_SET(0);
	delay_us(5);
	
	return ack;

}

//主机应答  0 应答  1  不应答
void IIC_MasterAck(uint8_t ack)
{
	
	IIC_SDA_Out();
	
	IIC_SCL_SET(0);
	IIC_SDA_SET(0);

	if (ack ==  0) 
		IIC_SDA_SET(0);
	else 
		IIC_SDA_SET(1);
	
	delay_us(5);
	
	IIC_SCL_SET(1);
	delay_us(5);
	
	IIC_SCL_SET(0);
	delay_us(5);
	
}





