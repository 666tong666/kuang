#include "EEPROM.h"

// 写入 , 读取

//不是必须的 , 为了保持 EEPROM_ 的队形
void EEPROM_Config(void)
{
	IIC_Config();
}

/*
	@brief 向指定地址 写入 一个字节的数据
	@param add 向哪个地址写入
	@param data 写入的数据是什么
	@retval 错误码
*/
uint8_t EEPROM_WriteByte(uint8_t add,uint8_t data)
{
	//开始工作
	IIC_Start();

	//寻址 10100000   0xA0
	IIC_SendByte(0xA0);
	if (IIC_SlaveAck() == 1) {
		printf("find slave add wrong");
		return 1;
	}
	
	//找到第几个格子
	IIC_SendByte(add);
	if (IIC_SlaveAck() == 1) {
		printf("find add in slave wrong");
		return 2;
	}
	
	//发数据
	IIC_SendByte(data);
	if (IIC_SlaveAck() == 1) {
		printf("write data wrong");
		return 3;
	}
	
	//关闭
	IIC_Stop();
	
	printf("write success!");
	return 0;
}

//指定一个地址 , 向这个地址 , 写入8个字节
//EEPROM   256个字节    8个  一页
/*
	@brief 向EEPROM指定页写入 最多 8个字节的数据
	@param pageAdd  页地址  自行计算 8 的倍数
	@param datasize 存入多少数据 小于8
	@param dataptr  数据指针
	@retval 错误码
*/
uint8_t EEPROM_PageWrite(uint8_t pageAdd, uint8_t datasize, uint8_t *dataptr)
{
	// 判断 pageAdd 是不是8的倍数 %8==0   datasize <= 8 
	IIC_Start();
	
	//寻址 10100000   0xA0
	IIC_SendByte(0xA0);
	if (IIC_SlaveAck() == 1) {
		IIC_Stop();
		printf("find slave add wrong");
		return 1;
	}
	
	//找到第几个格子
	IIC_SendByte(pageAdd);
	if (IIC_SlaveAck() == 1) {
		IIC_Stop();
		printf("find add in slave wrong");
		return 2;
	}
	
	while (datasize--)
	{
		IIC_SendByte(*dataptr++);
		if (IIC_SlaveAck() == 1) {
			IIC_Stop();
			printf("write data wrong");
			return 3;
		}
	}
		
	IIC_Stop();
	printf("write success");
	return 0;
}

/*
@brief 读取当前指针指向的地址的数据
@retval  数据

*/
uint8_t EEPROM_ReadCurrAdd(void)
{
	
	uint8_t data = 0;
	
	IIC_Start();
	
	//寻址 10100000   0xA0
	IIC_SendByte(0xA1);
	if (IIC_SlaveAck() == 1) {
		IIC_Stop();
		printf("find slave add wrong");
		return 1;
	}
	
	data = IIC_ReadByte();
	
	IIC_Stop();
	
	return data;
	
}

/*
	@brief  读取 指定地址的数据
	@param  要读取的地址
	@retval 读取到的数据
*/
uint8_t EEPROM_ReadByte(uint8_t Add)
{
	uint8_t data = 0;
	
	IIC_Start();
	
	//寻址 10100000   0xA0
	IIC_SendByte(0xA0);
	if (IIC_SlaveAck() == 1) {
		IIC_Stop();
		printf("find slave add wrong");
		return 1;
	}
	
	//修改指针
	IIC_SendByte(Add);
	if (IIC_SlaveAck() == 1) {
		IIC_Stop();
		printf("find add in slave wrong");
		return 1;
	}
	
	IIC_Stop();
	IIC_Start();
	//寻址 10100000   0xA0
	IIC_SendByte(0xA1);
	if (IIC_SlaveAck() == 1) {
		IIC_Stop();
		printf("find slave add wrong");
		return 1;
	}
	
	data = IIC_ReadByte();
	
	IIC_Stop();
	
	return data;

}

//测试读写好不好使
void EEPROM_Test()
{
	//接收到的数据
	uint8_t res_data = 0;
	
	
//	if (EEPROM_WriteByte(0x00,'a') != 0 )
//	{
//		printf("no!!! no!!");
//	
//	}
	delay_ms(500);
	
	res_data = EEPROM_ReadByte(0x00);
	
	printf("the data is: %c \r\n" , res_data);
	
	delay_ms(500);
}









