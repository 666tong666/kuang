#include "DMA.h"

void DMA_Config(uint32_t data_val, uint32_t memory_add)
{
	//使能时钟   注意这里手册不对!!!!!!
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);
	
	
	
	DMA_InitTypeDef mydma;
	//DMA_StructInit(&mydma);  把 mydma 配置成默认值 
	//选择通道
	mydma.DMA_Channel = DMA_Channel_4;
	//数据传输方向 内存 -> 外设
	mydma.DMA_DIR = DMA_DIR_MemoryToPeripheral;
	//传输模式 传一次就停 , 循环传输
	mydma.DMA_Mode = DMA_Mode_Normal;
	//数据传输量的大小 函数参数传递进来的  sizeof(iloveyou)
	mydma.DMA_BufferSize = data_val;
	//是否开启Fifo模式
	mydma.DMA_FIFOMode = DMA_FIFOMode_Disable;
	//FIfo里面的数据 到达多少 1/4、1/2、3/4、4/4 的时候 才发送
	//mydma.DMA_FIFOThreshold = 
	//要搬运的数据, 在内存中的地址
	mydma.DMA_Memory0BaseAddr = memory_add;
	//触发一次DMA, 发送几个 8bit  single代表1个
	mydma.DMA_MemoryBurst = DMA_MemoryBurst_Single;
	//单次发送多少数据  8bit (本次) / 16 / 32
	mydma.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
	//是否开启增量模式  读取数据的时候 指针是否自动下移
	mydma.DMA_MemoryInc = DMA_MemoryInc_Enable;
	
	////要搬运的数据, 在串口中的地址  发送数据寄存器的地址
	mydma.DMA_PeripheralBaseAddr = (uint32_t)&USART1->DR;
	mydma.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
	//单次接收多少数据
	mydma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	//串口不开启增量模式
	mydma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	//优先级
	mydma.DMA_Priority = DMA_Priority_VeryHigh;
	
	DMA_Init(DMA2_Stream7,&mydma);
	
	NVIC_InitTypeDef mynvic={0};
	
	//中断通道
	mynvic.NVIC_IRQChannel = DMA2_Stream7_IRQn;
	//使能
	mynvic.NVIC_IRQChannelCmd = ENABLE;
	//抢占优先级  0~15
	mynvic.NVIC_IRQChannelPreemptionPriority = 1;
	//响应优先级  0~15
	mynvic.NVIC_IRQChannelSubPriority = 1;
	
	NVIC_Init(&mynvic);
	
	//传输完毕 开启中断 
	DMA_ITConfig(DMA2_Stream7,DMA_IT_TC, ENABLE);
	
}

//   修一些DMA值  开启DMA 
void USART1_DMA_ENABLE(uint32_t data_val)
{
	//关闭
	DMA_Cmd(DMA2_Stream7,DISABLE);
	//保证关完了
	while (DMA_GetCmdStatus(DMA2_Stream7) != DISABLE);
	//修改 重置 本次发送数据量 
	DMA_SetCurrDataCounter(DMA2_Stream7,data_val);
	//清除标志位
	/*
	@arg DMA_FLAG_TCIFx:  传输完成
  @arg DMA_FLAG_HTIFx:  传输一般
  @arg DMA_FLAG_TEIFx:  传输出错
	*/
	DMA_ClearFlag(DMA2_Stream7,DMA_FLAG_TCIF7 | DMA_FLAG_HTIF7 | DMA_FLAG_TEIF7);
	
	
	//开启
	DMA_Cmd(DMA2_Stream7,ENABLE);
}

void DMA2_Stream7_IRQHandler(void)
{	
	if (DMA_GetITStatus(DMA2_Stream7,DMA_IT_TCIF7) == SET)
	{
		//重新调用发送的使能 , 传入要发送的数据的大小
		USART1_DMA_ENABLE(sizeof("loveyou\r\n")-1);
		DMA_ClearITPendingBit(DMA2_Stream7,DMA_IT_TCIF7);
	
	}
}





