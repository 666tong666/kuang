#include "mq135.h"

void ADC_MQ135_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct;
	ADC_InitTypeDef ADC_InitStruct;
	ADC_CommonInitTypeDef ADC_CommonInitStruct;

	//1.开启GPIOA、ADC1时钟
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);

	//2.PA4设置为模拟输入，关闭DAC，防止引脚复用冲突
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_4;
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AN;
	GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOA, &GPIO_InitStruct);

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, DISABLE); //关闭DAC，PA4复用DAC1_OUT

	//3.ADC通用配置（独立模式）
	ADC_CommonInitStruct.ADC_Mode = ADC_Mode_Independent;
	ADC_CommonInitStruct.ADC_Prescaler = ADC_Prescaler_Div4;
	ADC_CommonInitStruct.ADC_DMAAccessMode = ADC_DMAAccessMode_Disabled;
	ADC_CommonInitStruct.ADC_TwoSamplingDelay = ADC_TwoSamplingDelay_5Cycles;
	ADC_CommonInit(&ADC_CommonInitStruct);

	//4.ADC1参数
	ADC_InitStruct.ADC_Resolution = ADC_Resolution_12b;
	ADC_InitStruct.ADC_ScanConvMode = DISABLE;
	ADC_InitStruct.ADC_ContinuousConvMode = DISABLE;
	ADC_InitStruct.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;
	ADC_InitStruct.ADC_DataAlign = ADC_DataAlign_Right;
	ADC_InitStruct.ADC_NbrOfConversion = 1;
	ADC_Init(ADC1, &ADC_InitStruct);

	//5.配置ADC通道4（PA4=ADC1_IN4），采样时间长一点抗干扰
	ADC_RegularChannelConfig(ADC1, ADC_Channel_4, 1, ADC_SampleTime_480Cycles);

	//6.开启ADC1
	ADC_Cmd(ADC1, ENABLE);
}

//读取一次ADC值，返回0~4095
//读取一次ADC值，自动适配并修正左对齐问题
uint16_t MQ135_Get_ADC(void)
{
    uint16_t val;
    ADC_SoftwareStartConv(ADC1);        //软件触发转换
    while(ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET); //等待转换完成
    val = ADC_GetConversionValue(ADC1); //读取结果
    
    // 如果 ADC 的对齐方式被其他外设改成了左对齐（数值>4095），自动右移恢复正常 12 位
    if(val > 4095)
    {
        val = val >> 4;
    }
    
    return val;
}

//n次采样取平均，滤除ADC随机噪声（n建议8~32）
uint16_t MQ135_Get_ADC_Avg(uint8_t n)
{
    uint32_t sum = 0;
    uint8_t i;

    if(n == 0) n = 1;
    for(i = 0; i < n; i++)
    {
        ADC_SoftwareStartConv(ADC1);
        while(ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET);
        sum += ADC_GetConversionValue(ADC1);
    }
    return (uint16_t)(sum / n);
}