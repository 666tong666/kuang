#ifndef __APP_TASKS_H
#define __APP_TASKS_H

#include "stm32f4xx.h"

/* 系统事件位 (g_sysEvents) */
#define EVT_READY           (1 << 0)   /* MQ135 预热校准完成, 允许上报 */
#define EVT_ALARM_CHANGE    (1 << 1)   /* 告警状态变化, 请求立即补发一帧 */

/* 任务栈大小 (单位: 字, 1字=4字节) */
#define STACK_STARTUP       512
#define STACK_SENSOR        512
#define STACK_REPORT        768      /* MQTT_PublicTopic 内有 512 字节栈上组包缓冲 */
#define STACK_UI            384
#define STACK_BLE_CMD       256

void APP_CreateTasks(void);        /* main 中调用: 建队列/信号量/任务 */
void BLE_FrameIsrNotify(void);     /* USART2 空闲中断断到完整帧后调用 */
void APP_DisableKeyRecal(void);    /* 上电检测到 KEY0 异常时禁止重校 */

#endif
