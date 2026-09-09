#ifndef __RTC_H
#define __RTC_H

#include "stm32f4xx.h"
#include "Delay.h"
#include <stdio.h>

#define RTC_FLAG 1
#define LSE_OR_LSI 0    // 0 : LSE     1:LSI
#define YEAR 26
#define MONTH 8
#define DATE 6
#define WEEK 4
#define HOUR 9
#define MIN 0
#define SEC 0

#define A_HOUR 9
#define A_MIN 0
#define A_SEC 10
#define A_WEEK_DAY 4


void RTC_Config(void);
void RTC_Mode_Config(void);
void RTC_Time_Regulate(void);
void RTC_Alarm_Regulate(void);
void RTC_WakeUp_Config(void);
void RTC_ShowTime(void);

#endif