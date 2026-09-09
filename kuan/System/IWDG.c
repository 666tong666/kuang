#include "IWDG.h"

void IWDG_Config(void)
{
	//开启写使能  允许修改 看门狗里面的 各种东西 
	IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
	
	//预分频
	IWDG_SetPrescaler(IWDG_Prescaler_64);
	
	//设置重装载值  设置数多少个数;   1.2s之内喂狗
	IWDG_SetReload(100);
	
	//使能
	IWDG_Enable();
	
	//立即喂一次狗 ----- 立即重装载以下 
	IWDG_ReloadCounter();
}
	