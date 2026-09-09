#include "ADC.h"

void ADC_Config(void)
{
	//打开ADC的时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC3, ENABLE); 
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOF, ENABLE);
	
	GPIO_InitTypeDef mygpio;
	
	//引脚工作模式 : 模拟
	mygpio.GPIO_Mode = GPIO_Mode_AN;
	//引脚号
	mygpio.GPIO_Pin = ADC_PIN;
	//输入的工作模式 
	mygpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
	
	GPIO_Init(ADC_GROUP,&mygpio);
	
	
	ADC_InitTypeDef myadc;
	
	//连续转换 , ADC 转换完一次以后 , 要不要立即开始下一次转换
	myadc.ADC_ContinuousConvMode = DISABLE;
	//数据对齐   左 left   右 right  
	myadc.ADC_DataAlign = ADC_DataAlign_Right;
	//选择 通过哪个外部触发源转换ADC  
	//myadc.ADC_ExternalTrigConv = 
	//外部触发源 高电平 转换 还是 低电平 转换
	//myadc.ADC_ExternalTrigConvEdge = 
	//规则通道 转换的 总个数  1~16
	myadc.ADC_NbrOfConversion = 3;   //123
	//分辨率  , 3.3 / 若干份    如果选择 分辨率 12  相当于  3.3 / (2的12次幂份)
	myadc.ADC_Resolution = ADC_Resolution_12b;
	//是否开启扫描模式 , 扫描模式下 , 所有通道全部转换一次 , 触发一次中断
	myadc.ADC_ScanConvMode = DISABLE;
	
	ADC_Init(ADC3,&myadc);

	ADC_Cmd(ADC3,ENABLE);
}


uint16_t ADC_GetData(void)
{
	//配置本次使用的规则通道  
	//参数1 : ADC几  参数2 : 通道数字
	//参数3 : 多个通道进行转换 , 该数字用于排序
	//参数4 : 分频
	ADC_RegularChannelConfig(ADC3, ADC_Channel_5, 1, ADC_SampleTime_480Cycles);
	
	//开始转换   软件转换 : 通过代码开启转换
	ADC_SoftwareStartConv(ADC3);
	
	// ADC 消耗时间 
	while (ADC_GetFlagStatus(ADC3,ADC_FLAG_EOC) != SET);
		
	//返回编码数字  整数 , 返回值 用整数接取
	return ADC_GetConversionValue(ADC3);
}

// 27度  27.3  26.9  27.4 , 89  27.6  27.1  ..  
// ADC 采集的数据做 考虑 要不要做 滤波   滤波算法 很多很多 

// 做几次数据的滤波   ,  平均值滤波   24,5,6,1 / 4
float ADC_Get_Ave(uint8_t times)
{
	uint32_t res_val = 0;
	for (uint8_t i = 0; i < times; i++)
	{
		res_val += ADC_GetData();
	  delay_ms(5);
	}
	
	return res_val / times;
}

/*
@brief 获取转换完之后的光照强度
@params times 对ADC的结果,  做几次的平均值计算
@retval None
*/
uint16_t ADC_Get_Light(uint8_t times)
{
	float adc_ave;   //获取滤波后的平均值
	float adc_vol;   //计算电压值
	float adc_r;     //计算阻值
	uint16_t lux;
	adc_ave = ADC_Get_Ave(times);
	
	// 数字 * 每一份代表的电压数,   4096 取自于上面的 分辨率12 , 2的12次幂 = 4096
	adc_vol = adc_ave * (3.3/4096);
	
	// 电阻    R = U/I
	// R总 = R37 + R36
	/*
	R37 * 3.3 = U7*R36 + U7*R37
	R37(3.3-U7) = U7*R36
	R37 = (U7*R36) / (3.3-U7);
	
	看开发板 , 得到 光敏电阻旁边  , 竖直方向的电阻的阻值是 01C
	贴片电阻 的 文字 阻值如何计算 
	01c  代表 10k欧姆
	*/
	
	adc_r = (10000 * adc_vol) / (3.3 - adc_vol);
	printf("得到光敏电阻的阻值是:%f \r\n" , adc_r);
	
	// 查表
	lux = GetLux(adc_r);
	printf("得到的光照强度是:%d \r\n",lux);
	return lux;
}




