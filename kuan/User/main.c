/* ============================================================
 * main.c —— FreeRTOS 版入口
 *
 * 与裸机版的分工:
 *   main()     : 只做外设初始化(调度器启动前), 然后创建任务、启动调度器
 *   app_tasks.c: 全部业务逻辑 (采集/上报/显示/指令), 见该文件头部注释
 *
 * 关键差异:
 *   1. SysTick 交给 FreeRTOS 节拍使用, delay_us/ms 改为 DWT 忙等
 *   2. NVIC 分组必须为 Group_4 (全抢占), 且调用 FromISR API 的中断
 *      (USART1/2/3, UART4) 抢占优先级数值 >= 5
 *   3. ESP8266 联网(带死循环重试)移入 vReportTask: 无 WiFi 时仅云端
 *      通道不可用, 采集/显示/蓝牙照常工作 (裸机版会整机卡在开机)
 * ============================================================ */
#include "stm32f4xx.h"
#include "LED.h"
#include "Delay.h"
#include "KEY.h"
#include "USART.h"
#include <stdio.h>
#include "BEEP.h"
#include "oled.h"
#include "mqtt.h"
#include "esp8266.h"
#include "green.h"
#include "fan.h"
#include "mpu6050.h"
#include "mq135.h"
#include "FreeRTOS.h"
#include "task.h"
#include "app_tasks.h"

int main(void)
{
    // 1.系统基础配置
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);  /* FreeRTOS 要求全抢占分组 */
    BT24_PinsSafe();          /* 最先钳住PA2/PA3, 避免上电电平跳变干扰蓝牙模块 */
    DWT_Init();               /* DWT周期计数: 供delay_us/ms使用(不占SysTick) */
    delay_ms(2000);
    USART2_Config(BT24_BAUD);   /* PA2/PA3 -> DX-BT24 蓝牙, 默认9600 */
    USART_Config(115200);
    printf("System Starting... (FreeRTOS)\r\n");

    LED_Init();
    KEY_Config();
    Fan_Init();   /* 散热风扇：默认关闭，温度自动控制 */
    /* 上电检测KEY0(PG2): 内部上拉正常应读高。若上电即低(按键卡死/引脚被拉低/
     * 板子按键接法不是低有效), 自动关闭按键重校功能, 避免任务内死等卡死 */
    if(GPIO_ReadInputDataBit(KEY_GROUP, KEY0) == 0)
    {
        APP_DisableKeyRecal();
        printf("[WARN] KEY0(PG2) LOW at boot, key-recal disabled! (check key/wiring)\r\n");
    }
    BEEP_Init();
    OLED_Init();
    OLED_Clear();
    UART3_Config(115200);
    UART4_Config(115200);   /* PC10/PC11 -> K230 安全帽检测板 */

    //开机画面提示
    OLED_ShowString(1, 1, "Sys Initializing");
    delay_ms(500);

    ADC_MQ135_Init();
    MPU6050_Init();
    delay_ms(200);
    MPU6050_CalibrateGyro();

    /* MQ135预热+校准在 vStartupTask 内完成(120s, OLED倒计时),
     * 期间传感器任务照常采集, 上报任务等 READY 事件 */

    // 2.创建任务并启动调度器
    APP_CreateTasks();
    vTaskStartScheduler();

    /* 正常情况下不会运行到这里 (堆不足创建任务失败才会掉出) */
    printf("[RTOS-FATAL] scheduler returned!\r\n");
    while(1);
}
