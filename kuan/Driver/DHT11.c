#include "DHT11.h"
#include "FreeRTOS.h"
#include "task.h"

uint8_t humi_int = 0;
uint8_t humi_deci = 0;
uint8_t temp_int = 0;
uint8_t temp_deci = 0;
uint8_t check_sum = 0;
uint8_t dht_err = 1;

/* RTOS版注意:
 * 1. 40bit 位读取总时长约 4~5ms, 包在 taskENTER_CRITICAL 临界区内保证
 *    us 时序不被 RTOS 调度打断; 临界区内所有等待循环必须带超时上限,
 *    否则传感器异常时任务会带着全屏中断挂死
 * 2. 原版函数尾部有 delay_ms(200) 阻塞, 已移除 —— 读取节奏由
 *    vSensorTask 的 vTaskDelay(100) 节拍控制 (每2拍读一次) */

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

/* 等待数据线变为指定电平, 超时返回1 (临界区内安全: 纯循环+计数)
 * 上限1000次约150~250us: 正常最长信号为响应的80us电平, 须留足余量;
 * 同时保证传感器异常时临界区的封锁时间有界 */
static uint8_t DHT11_WaitLevel(uint8_t level)
{
	uint32_t count = 0;
	while(DHT11_Read() != level)
	{
		if(++count > 1000) return 1;
	}
	return 0;
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
	DHT11_Input_Mode();
	if(DHT11_Read() == 0)
	{
		if(DHT11_WaitLevel(1) != 0) return 1;   /* 低电平(响应80us)超时 */
		if(DHT11_WaitLevel(0) != 0) return 2;   /* 高电平(准备80us)超时 */
		return 0;
	}
	return 3;
}

uint8_t DHT11_Get(void)
{
	uint8_t temp = 0;
	uint8_t i;
	for(i = 0; i < 8; i++)
	{
		/* 位格式: 50us低 + 高电平(0=26~28us / 1=70us)
		 * 先等低电平结束(高电平开始) —— 与原版 while(read==0) 语义一致 */
		if(DHT11_WaitLevel(1) != 0) return temp;
		delay_us(35);                 /* 35us采样: 0已拉低判0, 1仍为高判1 */
		if(DHT11_Read() == 1)
		{
			if(DHT11_WaitLevel(0) != 0) return temp;   /* 等本位高电平结束 */
			temp |= 0x01 << (7 - i);
		}
	}
	return temp;
}

/* RTOS版: 整个事务(起始拉低20ms + 40bit约5ms)整体包进临界区 ——
 * sensor/startup 同优先级, 若只在位读取段加临界区, 1ms 节拍可能
 * 在起始脉冲与读数之间切到别的任务(OLED位操作), 撕碎时序后读回
 * 乱值(温湿度相同/超量程/卡85)。事务内所有等待均带超时, 传感器
 * 异常时封锁时间有界。采样节奏由调用方保证 >= 1s (DHT11 规格要求) */
void DHT11_Read_Data(void)
{
	uint8_t h1, h2, t1, t2, cs;

	taskENTER_CRITICAL();
	DHT11_Start();
	if(DHT11_Back() == 0)
	{
		h1 = DHT11_Get();
		h2 = DHT11_Get();
		t1 = DHT11_Get();
		t2 = DHT11_Get();
		cs = DHT11_Get();

		DHT11_Output_Mode();
		DHT11_Set(1);
		taskEXIT_CRITICAL();

		/* 校验通过才提交全局值: 失败时保留上一次有效读数, 乱值不上屏 */
		if(cs == (uint8_t)(h1 + h2 + t1 + t2))
		{
			humi_int  = h1;
			humi_deci = h2;
			temp_int  = t1;
			temp_deci = t2;
			check_sum = cs;
			dht_err = 0;
		}
		else
		{
			dht_err = 1;
		}
	}
	else
	{
		DHT11_Output_Mode();
		DHT11_Set(1);
		taskEXIT_CRITICAL();
		dht_err = 1;
	}
}
