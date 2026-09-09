#include "DAC.h"


void DAC_Config(void)
{
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
	
	GPIO_InitTypeDef mygpio;
	
	//引脚工作模式 : 模拟
	mygpio.GPIO_Mode = GPIO_Mode_AN;
	//引脚号
	mygpio.GPIO_Pin = DAC_PIN;
	//输入的工作模式 
	mygpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
	
	GPIO_Init(DAC_GROUP,&mygpio);
	
	DAC_InitTypeDef mydac = {0};
	
	//是否开启缓冲
	mydac.DAC_OutputBuffer = DAC_OutputBuffer_Disable;
	//设置触发源 , 定时器 , 引脚 , 软件 , 自动
	mydac.DAC_Trigger = DAC_Trigger_None;
	//禁用波形生成 , 如果不禁用 , 可以选择 噪声波 或 三角波
	mydac.DAC_WaveGeneration = DAC_WaveGeneration_None;
	//调噪声波区间 , 和 三角波的峰值
	//mydac.DAC_LFSRUnmask_TriangleAmplitude = 
	
	DAC_Init(DAC_Channel_2,&mydac);
	
	
	DAC_Cmd(DAC_Channel_2,ENABLE);
}

// 函数  xxxx(2, 数字)