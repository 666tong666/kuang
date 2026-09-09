#include "RTC.h"

void RTC_Config(void)
{

	if (RTC_ReadBackupRegister(RTC_BKP_DR0) != RTC_FLAG) {

		//选择时钟和分频()
		RTC_Mode_Config();
		//设置时间
		RTC_Time_Regulate();
		//闹钟()
		RTC_Alarm_Regulate();
		
		//向RTC的备份寄存器写入内容 , 数据不会随着关机 没电 丢失
		RTC_WriteBackupRegister(RTC_BKP_DR0,RTC_FLAG);
		printf("RTC First Time set success");
		
	} else {
		//其他
		RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR,ENABLE);
		//开启访问	权限
		PWR_BackupAccessCmd(ENABLE);
		//等待时钟同步
		RTC_WaitForSynchro();
		//清除标志位
		EXTI_ClearITPendingBit(EXTI_Line17);
		//清除闹钟中断标志位
		RTC_ClearFlag(RTC_FLAG_ALRAF);
		
		printf("Dont need to configure RTC again");
	}
	
	//配置唤醒功能
	RTC_WakeUp_Config();

}
//选择时钟和分频
void RTC_Mode_Config(void)
{
	//开启PWR的时钟信号
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR,ENABLE);
	
	//RCC 断电数据不丢失 , PWR 备份寄存器 , 开启寄存器访问权限
	PWR_BackupAccessCmd(ENABLE);
	
	uint16_t AsyncPSC = 0;
	uint16_t SyncPSC = 0;
	
	if (LSE_OR_LSI == 0) {
		//开启LSE
		RCC_LSEConfig(RCC_LSE_ON);
		//等待时钟信号校准完毕
		while(RCC_GetFlagStatus(RCC_FLAG_LSERDY) != SET);
		//为RTC选择时钟源
		RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
		AsyncPSC = 0x7F;  //127
		SyncPSC = 0xFF;   //255
		
	} else {
		//LSI
		RCC_LSICmd(ENABLE);
		//等待时钟信号校准完毕
		while(RCC_GetFlagStatus(RCC_FLAG_LSIRDY) != SET);
		//为RTC选择时钟源
		RCC_RTCCLKConfig(RCC_RTCCLKSource_LSI);
		AsyncPSC = 0x7F;  //127
		SyncPSC = 0xF9;   //249
	}
	
	//开启RTC时钟
	RCC_RTCCLKCmd(ENABLE);
	//等待时钟同步
	RTC_WaitForSynchro();
	//配置RTC
	RTC_InitTypeDef myrtc;
	// 异步 Async
	myrtc.RTC_AsynchPrediv = AsyncPSC;
	// 同步 sync
	myrtc.RTC_SynchPrediv = SyncPSC;
	// 1HZ
	myrtc.RTC_HourFormat = RTC_HourFormat_24;
	RTC_Init(&myrtc);
	
}

//时间
void RTC_Time_Regulate(void)
{
	RTC_TimeTypeDef mytime;
	//上面如果选择 12时制 , 这个才生效  AM 上午  PM下午
	mytime.RTC_H12 = RTC_H12_AM;
	mytime.RTC_Hours = HOUR;    //时
	mytime.RTC_Minutes = MIN;		//分
	mytime.RTC_Seconds = SEC;		//秒
	RTC_SetTime(RTC_Format_BIN,&mytime); //配置时间函数
	
	RTC_DateTypeDef mydate;
	
	mydate.RTC_Year = YEAR;			//年
	mydate.RTC_Month = MONTH;		//月
	mydate.RTC_Date = DATE;			//日
	mydate.RTC_WeekDay = WEEK;	//星期
	
	RTC_SetDate(RTC_Format_BIN,&mydate);	//配置日期函数
	
}
//闹钟
void RTC_Alarm_Regulate(void)
{
	
	RTC_AlarmTypeDef myalerm;
	
	myalerm.RTC_AlarmTime.RTC_H12 = RTC_H12_AM;
	myalerm.RTC_AlarmTime.RTC_Hours = A_HOUR;
	myalerm.RTC_AlarmTime.RTC_Minutes = A_MIN;
	myalerm.RTC_AlarmTime.RTC_Seconds = A_SEC;
	
	//匹配几号响 , 还是星期几响
	myalerm.RTC_AlarmDateWeekDaySel = RTC_AlarmDateWeekDaySel_WeekDay;
	//上面配合 , 上面选择星期,这里设置星期几 , 上面选择日期,这里是设置几号
	myalerm.RTC_AlarmDateWeekDay = A_WEEK_DAY;
	
	myalerm.RTC_AlarmMask = RTC_AlarmMask_DateWeekDay;
	
	/*
	RTC_AlarmMask_None 						 刚刚设置的值全部生效        
	RTC_AlarmMask_DateWeekDay      日期或者星期无效 
	RTC_AlarmMask_Hours     小时无效         
	RTC_AlarmMask_Minutes   分钟  
	RTC_AlarmMask_Seconds   秒无效      
	RTC_AlarmMask_All       全部无效 (闹钟不响)          	
	*/
	
	RTC_SetAlarm(RTC_Format_BIN,RTC_Alarm_A,&myalerm);
	
	EXTI_ClearITPendingBit(EXTI_Line17);
	//配置17号中断线 , 上升沿有效
	EXTI_InitTypeDef myexit;
	
	// 哪几根中断线
	myexit.EXTI_Line = EXTI_Line17;
	//使能
	myexit.EXTI_LineCmd = ENABLE;
	//本次工作模式 中断 / 事件  
	myexit.EXTI_Mode = EXTI_Mode_Interrupt;
	//触发条件: 下降沿
	myexit.EXTI_Trigger = EXTI_Trigger_Rising;
	
	EXTI_Init(&myexit);
	
	NVIC_InitTypeDef mynvic={0};
	
	//中断通道
	mynvic.NVIC_IRQChannel = RTC_Alarm_IRQn;
	//使能
	mynvic.NVIC_IRQChannelCmd = ENABLE;
	//抢占优先级  0~15
	mynvic.NVIC_IRQChannelPreemptionPriority = 1;
	//响应优先级  0~15
	mynvic.NVIC_IRQChannelSubPriority = 1;
	
	NVIC_Init(&mynvic);
	
	//开启闹钟A的中断
	RTC_ITConfig(RTC_IT_ALRA,ENABLE);
	//保险 , 清除闹钟A标志位 
	RTC_ClearFlag(RTC_FLAG_ALRAF);
	
	RTC_AlarmCmd(RTC_Alarm_A, ENABLE);
}

//唤醒中断
void RTC_WakeUp_Config(void)
{
	//先关闭 配置完 再打开 
	RTC_WakeUpCmd(DISABLE);
	
	
	EXTI_ClearITPendingBit(EXTI_Line22);
	//配置17号中断线 , 上升沿有效
	EXTI_InitTypeDef myexit;
	
	// 哪几根中断线
	myexit.EXTI_Line = EXTI_Line22;
	//使能
	myexit.EXTI_LineCmd = ENABLE;
	//本次工作模式 中断 / 事件  
	myexit.EXTI_Mode = EXTI_Mode_Interrupt;
	//触发条件: 下降沿
	myexit.EXTI_Trigger = EXTI_Trigger_Rising;
	
	EXTI_Init(&myexit);
	
	
	NVIC_InitTypeDef mynvic={0};
	//中断通道
	mynvic.NVIC_IRQChannel = RTC_WKUP_IRQn;
	//使能
	mynvic.NVIC_IRQChannelCmd = ENABLE;
	//抢占优先级  0~15
	mynvic.NVIC_IRQChannelPreemptionPriority = 1;
	//响应优先级  0~15
	mynvic.NVIC_IRQChannelSubPriority = 1;
	
	NVIC_Init(&mynvic);
	
	//配置唤醒中断的时钟源   PSC  分频
	RTC_WakeUpClockConfig(RTC_WakeUpClock_CK_SPRE_16bits);
	
	// 重装载值 ARR   1s 数一次  数1个数 = 1s 唤醒一次
	RTC_SetWakeUpCounter(0x0);
	//清除唤醒中断标志位
	RTC_ClearITPendingBit(RTC_IT_WUT);
	//开启唤醒中断
	RTC_ITConfig(RTC_IT_WUT,ENABLE);
	//使能唤醒功能
	RTC_WakeUpCmd(ENABLE);
}

//显示年月日时分秒
void RTC_ShowTime(void)
{
	char showtime[10] = {0};
	char showdate[10] = {0};
	RTC_TimeTypeDef mytime;
	
	RTC_GetTime(RTC_Format_BIN,&mytime);
	
	sprintf(showtime,"%d:%d:%d",mytime.RTC_Hours,mytime.RTC_Minutes,mytime.RTC_Seconds);
	
	RTC_DateTypeDef mydate;
	RTC_GetDate(RTC_Format_BIN,&mydate);
	
	sprintf(showdate,"%d-%d-%d",mydate.RTC_Year,mydate.RTC_Month,mydate.RTC_Date);
	//展示时间
	printf("showtime: %s %s\r\n",showdate,showtime);
}

//闹钟 
void RTC_Alarm_IRQHandler(void)
{
	if(RTC_GetITStatus(RTC_IT_ALRA) == SET)
	{
		printf("alerm is work now...");
		
		EXTI_ClearITPendingBit(EXTI_Line17);
		RTC_ClearFlag(RTC_FLAG_ALRAF);
	}
	
}

//唤醒 
void RTC_WKUP_IRQHandler(void)
{

	if(RTC_GetITStatus(RTC_IT_WUT) == SET)
	{
		
		RTC_ShowTime();
		EXTI_ClearITPendingBit(EXTI_Line22);
		RTC_ClearFlag(RTC_FLAG_WUTF);
	}

}



