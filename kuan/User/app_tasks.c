/* ============================================================
 * app_tasks.c —— FreeRTOS 版应用层
 *
 * 裸机主循环 (100ms 前后台) 拆分为 5 个任务:
 *   vStartupTask  prio4  MQ135 预热 120s + R0 校准 -> 置 READY
 *   vSensorTask   prio4  100ms 节拍: MPU6050 震动 / DHT11(200ms) /
 *                        MQ135 采样 -> 快照写入 g_env -> 告警变化触发补发
 *   vReportTask   prio3  先完成 ESP8266+MQTT 接入; 之后 5s 周期 +
 *                        事件驱动 组帧 MQTT 上报 + BLE 透传
 *   vUITask       prio2  100ms: KEY0 重校扫描 / 蜂鸣器·LED·风扇联动 /
 *                        每 500ms 刷新 OLED
 *   vBLECmdTask   prio3  等串口空闲中断信号量 -> 解析 BLE 下发指令
 *
 * 共享数据: g_env (环境快照) 由 g_dataMutex 保护;
 *          k230 计数由 UART4 中断写、各任务读 (uint8 原子, 与裸机一致)
 * ============================================================ */
#include "stm32f4xx.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"
#include "app_tasks.h"
#include "Delay.h"
#include "USART.h"
#include "LED.h"
#include "green.h"
#include "KEY.h"
#include "BEEP.h"
#include "DHT11.h"
#include "oled.h"
#include "mq135.h"
#include "mpu6050.h"
#include "mqtt.h"
#include "esp8266.h"
#include "fan.h"

/* ---------------- 阈值配置 (与裸机版一致) ---------------- */
#define TEMP_MAX       32          /* 温度上限 ℃ */
float gas_max = 50.0f;             /* 气体等效ppm上限(可由小程序经BLE下发修改) */

/* MQ135校准参数 */
#define RL      10.0f               /* 负载电阻 kΩ */
#define R0      12.50f
#define MQ135_CLEAN_RS_RATIO  3.4f
#define MQ135_WARMUP_S        120

/* 震动检测参数: 8192 LSB = 0.5g (±2g量程 16384 LSB/g) */
uint16_t g_shake_threshold = 8192;
#define SHAKE_CNT         5        /* 连续N次超限确认震动 (5次x110ms≈0.55s, 对齐裸机调参时的时间窗) */
#define SHAKE_RELEASE_CNT 5        /* 震动消失后再等5个周期解除锁存 */

#define MPU_DEBUG_PRINT 0          /* RTOS版默认关闭, 排查误触发时改1 */

/* ---------------- RTOS 对象 ---------------- */
static SemaphoreHandle_t g_dataMutex;    /* 保护 g_env / 上报粘性标志 */
static SemaphoreHandle_t g_bleCmdSem;    /* USART2空闲中断 -> 命令任务 */
static EventGroupHandle_t g_sysEvents;   /* EVT_READY / EVT_ALARM_CHANGE */

/* ---------------- 共享数据 ---------------- */
typedef struct
{
    float   temp, humi, gas;
    uint8_t helmet, head;      /* K230 人数 */
    uint8_t shake_flag;        /* 震动瞬时锁存 */
} env_data_t;

static env_data_t g_env;
static volatile uint8_t g_shake_pending = 0;   /* 上报粘性标志 */
static volatile uint8_t g_last_alarm_sent = 0; /* 最近一次上报的告警掩码 */
static volatile uint8_t g_sys_ready = 0;       /* MQ135 校准完成 */
static uint8_t key0_recal_en = 1;              /* KEY0重校功能开关 */

/* MQ135 校准后的R0(kΩ), 上电自动校准, KEY0长按可随时重校 */
static float gas_r0 = R0;

/* DHT11 输出全局 (DHT11.c 定义) */
extern uint8_t humi_int, humi_deci, temp_int, temp_deci, dht_err;

/* USART.c 定义 */
extern volatile uint8_t  bt24_rx_done;
extern char bt24_rx_buf[];

/* USART.c 定义: K230 计数 */
extern volatile uint8_t  k230_helmet_cnt;
extern volatile uint8_t  k230_head_cnt;
extern volatile uint16_t k230_silence;
#define K230_TIMEOUT_ROUNDS  50   /* 50*100ms 无数据 -> 判离线 */

/* ================ 震动检测 (原裸机 main.c) ================ */
/* SHAKE_CNT 从裸机版的3提到5: 裸机主循环实际周期约200~400ms(DHT11阻塞
 * +OLED刷新), "连续3次超限"实际要求约0.4~1s的持续震动; RTOS版采样精确
 * 到约110ms, 3次只要0.33s —— 灵敏度被动提高了2~3倍, 桌面传导的余振会
 * 误触发(状态跳变)。5次x110ms约0.55s, 恢复裸机调参时的严格程度 */
static uint8_t MPU6050_VibrationDetect(int16_t ax, int16_t ay, int16_t az)
{
    static int16_t ax_last = 0, ay_last = 0, az_last = 0;
    static int16_t dx_last = 0, dy_last = 0, dz_last = 0;
    static uint8_t cnt = 0;
    static uint8_t vib_latch = 0;
    static uint8_t release_timer = 0;

    if(abs(ax) > 32000 || abs(ay) > 32000 || abs(az) > 32000)
    {
        ax = ax_last; ay = ay_last; az = az_last;
    }

    {
        int16_t delta_ax = abs(ax - ax_last);
        int16_t delta_ay = abs(ay - ay_last);
        int16_t delta_az = abs(az - az_last);

        ax_last = ax; ay_last = ay; az_last = az;

        if(delta_ax > g_shake_threshold || delta_ay > g_shake_threshold
           || delta_az > g_shake_threshold)
        {
            cnt++;
            release_timer = 0;
            dx_last = delta_ax; dy_last = delta_ay; dz_last = delta_az;
#if MPU_DEBUG_PRINT
            printf("[MPU] over! dx=%d dy=%d dz=%d (thr=%u) cnt=%d/%d\r\n",
                   delta_ax, delta_ay, delta_az, g_shake_threshold, cnt, SHAKE_CNT);
#endif
            if(cnt >= SHAKE_CNT)
            {
                if(vib_latch == 0)
                {
                    /* 触发瞬间打印差值: 用于区分真实冲击(远大于阈值)和
                     * 电气毛刺/余振(贴着阈值), 再决定调阈值还是查供电 */
                    printf("[MPU] SHAKE LATCH ON dx=%d dy=%d dz=%d (thr=%u)\r\n",
                           dx_last, dy_last, dz_last, g_shake_threshold);
                }
                vib_latch = 1;
                g_shake_pending = 1;
            }
        }
        else
        {
            cnt = 0;
            if(vib_latch == 1)
            {
                release_timer++;
                if(release_timer >= SHAKE_RELEASE_CNT)
                {
                    vib_latch = 0;
                    release_timer = 0;
                    /* 事件已结束: 粘性标志还没被上报取走则丢弃, 避免拖慢恢复帧 */
                    g_shake_pending = 0;
#if MPU_DEBUG_PRINT
                    printf("[MPU] shake latch released\r\n");
#endif
                }
            }
        }
    }
    return vib_latch;
}

/* ================ MQ135 校准与补偿 (原裸机 main.c) ================ */

static float MQ135_CorrectionFactor(float t, float h)
{
    if(t <= 0.0f && h <= 0.0f) return 1.0f;
    if(t < 20.0f)
        return 0.00035f*t*t - 0.02718f*t + 1.39538f - (h - 33.0f)*0.0018f;
    return -0.003333333f*t - 0.001923077f*h + 1.130128205f;
}

/* RTOS版: 在干净空气中校准R0 —— 64次采样, 每次间隔用 vTaskDelay(100)
 * (原版 delay_ms(100) 忙等会占住CPU, 任务上下文必须让出) */
static void MQ135_Calibrate(void)
{
    uint32_t sum = 0;
    uint8_t  k;
    uint16_t adc;
    float    Rs;

    for(k = 0; k < 64; k++)
    {
        sum += MQ135_Get_ADC();
        vTaskDelay(pdMS_TO_TICKS(100));    /* 6.4s长窗口, 压制±10%短时波动 */
    }
    adc = (uint16_t)(sum / 64);
    if(adc < 10) adc = 10;
    Rs = RL * (4095.0f / (float)adc - 1.0f);
    gas_r0 = Rs / MQ135_CLEAN_RS_RATIO;
    printf("MQ135 cal: adc=%d Rs=%.2fk R0=%.2fk\r\n", adc, Rs, gas_r0);

    if(adc >= 4000)
    {
        gas_r0 = R0;
        printf("[MQ135][ERR] adc=%d 顶格! 排查: 1)是否误接模块DO脚(应接AO/AOUT) "
               "2)5V供电模块AOUT可超3.3V, 需分压或改3.3V供电 3)传感器内阻过低\r\n", adc);
    }
    else if(adc <= 20)
    {
        gas_r0 = R0;
        printf("[MQ135][ERR] adc=%d 贴底! 排查: AO信号线未接好 / 模块未供电 / "
               "加热丝开路(传感器两脚间应有约1V压降)\r\n", adc);
    }
    else if(gas_r0 < 1.0f || gas_r0 > 500.0f)
    {
        printf("[MQ135][WARN] R0=%.2fk 超出1~500k合理区间, 已限幅\r\n", gas_r0);
        if(gas_r0 < 1.0f)   gas_r0 = 1.0f;
        if(gas_r0 > 500.0f) gas_r0 = 500.0f;
    }
}

/* ================ BLE 下行指令处理 (原裸机 main.c) ================ */
static void BLE_HandleCommand(char *buf)
{
    char *p;

    if(strstr(buf, "gas_max") != NULL)
    {
        p = strstr(buf, "val");
        if(p != NULL)
        {
            p = strchr(p, ':');
            if(p != NULL)
            {
                float val = strtof(p + 1, NULL);
                if(val >= 1.0f && val <= 9999.0f)
                {
                    char ack[48];
                    gas_max = val;
                    snprintf(ack, sizeof(ack),
                             "{\"cmd\":\"gas_max\",\"ack\":%.0f}\r\n", gas_max);
                    BT24_SendString(ack);
                    printf("BLE cmd: gas_max -> %.0f ppm\r\n", gas_max);
                }
                else
                {
                    BT24_SendString("{\"cmd\":\"gas_max\",\"ack\":err}\r\n");
                    printf("BLE cmd: gas_max value out of range: %.1f\r\n", val);
                }
            }
        }
    }

    bt24_rx_len  = 0;
    bt24_rx_buf[0] = '\0';
    bt24_rx_done = 0;
}

/* ================ 任务: 启动 (MQ135 预热+校准) ================ */
static void vStartupTask(void *pv)
{
    uint16_t waited_s = 0;

    (void)pv;
    printf("MQ135 warming up %ds, keep clean air...\r\n", MQ135_WARMUP_S);
    OLED_Clear();
    OLED_ShowString(1, 1, "MQ135 warming up");
    OLED_ShowString(2, 1, "keep clean air!");

    while(waited_s < MQ135_WARMUP_S)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));      /* 原版阻塞 delay_ms(1000) */
        waited_s++;

        OLED_ShowString(3, 1,  "warm ");
        OLED_ShowNum(3, 6,  waited_s, 3);
        OLED_ShowString(3, 9,  "/120s");

        if(waited_s % 10 == 0)
        {
            printf("  warm %us: adc=%d\r\n", waited_s, MQ135_Get_ADC_Avg(16));
        }
    }
    printf("MQ135 warmup finished, calibrating...\r\n");
    MQ135_Calibrate();
    OLED_Clear();

    g_sys_ready = 1;
    xEventGroupSetBits(g_sysEvents, EVT_READY);
    printf("Initialization Complete! (FreeRTOS)\r\n");
    vTaskDelete(NULL);
}

/* ================ 任务: 传感器采集 (100ms 节拍) ================ */
static void vSensorTask(void *pv)
{
    int16_t ax, ay, az, gx, gy, gz;
    float nh3_ppm;
    uint16_t adc_val;
    uint8_t dht_tick = 0;
    uint8_t shake_flag = 0;

    (void)pv;
    for(;;)
    {
        /* K230 在线检测计数 (UART4 中断收到数据清0) */
        if(k230_silence < 60000) k230_silence++;

        /* DHT11: 每1s读一次 (规格要求采样间隔>=1s; 裸机版靠函数尾部
         * 200ms阻塞+慢主循环碰巧满足, RTOS版按节拍精确保证) */
        dht_tick++;
        if(dht_tick >= 10)
        {
            dht_tick = 0;
            DHT11_Read_Data();
        }

        MPU6050_ReadAll(&ax, &ay, &az, &gx, &gy, &gz);
        shake_flag = MPU6050_VibrationDetect(ax, ay, az);
#if MPU_DEBUG_PRINT
        printf("[MPU] ax=%d ay=%d az=%d lat=%d pend=%d\r\n",
               ax, ay, az, shake_flag, g_shake_pending);
#endif

        /* MQ135: 16次均值 + 温湿度修正 + 校准后的R0 */
        adc_val = MQ135_Get_ADC_Avg(16);
        if(adc_val < 10) adc_val = 10;
        if(gas_r0 < 0.01f) gas_r0 = R0;
        {
            float temp_now = temp_int + temp_deci / 10.0f;
            float humi_now = humi_int + humi_deci / 10.0f;
            float Rs = RL * (4095.0f / (float)adc_val - 1.0f);
            Rs /= MQ135_CorrectionFactor(temp_now, humi_now);
            {
                float rs_r0 = Rs / gas_r0;
                if(rs_r0 < 0.01f) rs_r0 = 0.01f;
                nh3_ppm = 102.2f * powf(rs_r0, -2.473f);
            }
            if(nh3_ppm < 1.0f)   nh3_ppm = 1.0f;
            if(nh3_ppm > 9999.0f) nh3_ppm = 9999.0f;
            printf("ADC:%d Rs:%.2f R0:%.2f PPM:%.1f\r\n", adc_val, Rs, gas_r0, nh3_ppm);
        }

        /* 快照写入共享区 */
        if(xSemaphoreTake(g_dataMutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            g_env.temp  = temp_int + temp_deci / 10.0f;
            g_env.humi  = humi_int + humi_deci / 10.0f;
            g_env.gas   = nh3_ppm;
            g_env.helmet = k230_helmet_cnt;
            g_env.head   = k230_head_cnt;
            g_env.shake_flag = shake_flag;
            xSemaphoreGive(g_dataMutex);
        }

        /* 事件驱动上报: 告警掩码与最近上报帧不同 -> 请求立即补发 */
        if(g_sys_ready)
        {
            uint8_t now_alarm = 0;
            if (g_env.temp > TEMP_MAX) now_alarm |= 0x01;
            if (g_env.gas  > gas_max)  now_alarm |= 0x02;
            if (shake_flag || g_shake_pending) now_alarm |= 0x04;
            if (now_alarm != g_last_alarm_sent)
            {
                xEventGroupSetBits(g_sysEvents, EVT_ALARM_CHANGE);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ================ 任务: 上报 (ESP8266接入 + 5s周期/事件驱动) ================ */
static void vReportTask(void *pv)
{
    uint8_t ping_cnt = 0;

    (void)pv;
    /* 联网流程较长(带重试), 放在任务里跑: vTaskDelay 等待期间不影响其他任务;
     * 裸机版是开机死等联网, 无 WiFi 时整机卡死 —— RTOS版仅上报通道不可用 */
    ESP8266_Init();
    printf("Cloud channel ready\r\n");

    for(;;)
    {
        EventBits_t bits;

        /* 5s 周期超时 或 告警变化事件 唤醒 */
        bits = xEventGroupWaitBits(g_sysEvents,
                                   EVT_READY | EVT_ALARM_CHANGE,
                                   pdTRUE, pdFALSE,
                                   pdMS_TO_TICKS(5000));
        if((bits & EVT_READY) == 0 && g_sys_ready == 0)
        {
            continue;    /* 预热未完成, 不上报 (气体值无意义) */
        }

        if(xSemaphoreTake(g_dataMutex, pdMS_TO_TICKS(200)) == pdTRUE)
        {
            float temp_val = g_env.temp;
            float humi_val = g_env.humi;
            float gas_val  = g_env.gas;
            uint8_t helmet = g_env.helmet;
            uint8_t head   = g_env.head;
            /* 震动取 "瞬时锁存 | 粘性标志", 取走即清 */
            uint8_t shake_report = g_env.shake_flag | g_shake_pending;
            g_shake_pending = 0;
            xSemaphoreGive(g_dataMutex);

            {
                uint8_t status = (temp_val > TEMP_MAX || gas_val > gas_max
                                  || shake_report) ? 1 : 0;
                uint8_t alarm = 0;
                if (temp_val > TEMP_MAX)   alarm |= 0x01;
                if (gas_val  > gas_max)    alarm |= 0x02;
                if (shake_report)          alarm |= 0x04;
                g_last_alarm_sent = alarm;
                {
                    uint8_t fan = (temp_val > TEMP_MAX || gas_val > gas_max) ? 1 : 0;

                    /* MQTT上报 (v2.1: +helmet+head) */
                    MQTT_PublicTopic(humi_val, temp_val, gas_val, fan, alarm, helmet, head);
                    printf("MQTT upload: temp=%.1f humi=%.1f gas=%.1f fan=%u alarm=%u helmet=%u head=%u\r\n",
                           temp_val, humi_val, gas_val, fan, alarm, helmet, head);
                    /* BLE透传同一JSON帧 */
                    BT24_SendFrame(temp_val, humi_val, gas_val, status, fan, alarm, helmet, head);
                }
            }
        }

        /* 每30s(约6次上报)做一次MQTT保活探测: PINGREQ 等不到 PINGRESP
         * 说明链路已断(WiFi掉线/TCP被服务端关闭等)。数据 publish 是
         * QoS0 单向的, 断线时静默失败, 不主动探测就永远发现不了。
         * 重连直接重走 ESP8266_Init: 内部先 AT+RST 重置模块, 再
         * 连WiFi->TCP->MQTT CONNECT->订阅, 覆盖所有断线形态 */
        if(++ping_cnt >= 6)
        {
            ping_cnt = 0;
            if(ESP8266_MQTT_PingCheck() == 0)
            {
                printf("MQTT link lost! reconnecting...\r\n");
                ESP8266_Init();
                printf("Cloud channel restored\r\n");
            }
        }
    }
}

/* ================ 任务: 本地交互 (按键/声光/OLED) ================ */
static void vUITask(void *pv)
{
    static uint16_t key0_hold_ms = 0;
    uint8_t oled_tick = 0;

    (void)pv;
    for(;;)
    {
        /* MQ135 预热/校准期间(startup 任务)独占 OLED 显示权:
         * 此时不刷数据页(气体值未校准无意义)、不做声光联动(避免
         * 未校准气体乱值触发蜂鸣器)、不扫 KEY0(校准无意义) */
        if(g_sys_ready == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* KEY0(PG2) 长按1.5s: 干净空气中重新校准R0 (校准阻塞本任务6.4s,
         * 期间OLED暂停刷新, 与裸机版行为一致) */
        if(key0_recal_en && GPIO_ReadInputDataBit(KEY_GROUP, KEY0) == 0)
        {
            key0_hold_ms += 100;
            if(key0_hold_ms >= 1500)
            {
                key0_hold_ms = 0;
                BEEP_Beep(100);
                printf("MQ135 recalibrating...\r\n");
                MQ135_Calibrate();
                BEEP_Beep(100);
                {
                    uint16_t guard = 0;
                    while(GPIO_ReadInputDataBit(KEY_GROUP, KEY0) == 0 && guard < 30)
                    {
                        vTaskDelay(pdMS_TO_TICKS(100));
                        guard++;
                    }
                }
            }
        }
        else
        {
            key0_hold_ms = 0;
        }

        /* 告警联动: 任一告警 -> 蜂鸣器响+LED灭, 正常 -> LED亮+蜂鸣器关
         * (与裸机版阈值判断一致; 风扇跟随温度/气体) */
        if(xSemaphoreTake(g_dataMutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            uint8_t danger = (g_env.temp > TEMP_MAX || g_env.gas > gas_max
                              || g_env.shake_flag == 1) ? 1 : 0;
            uint8_t fan    = (g_env.temp > TEMP_MAX || g_env.gas > gas_max) ? 1 : 0;
            xSemaphoreGive(g_dataMutex);

            if (danger) { LED_Off(); BEEP_On(); }
            else        { LED_On();  BEEP_Off(); }
            if (fan) Fan_On(); else Fan_Off();
        }

        /* 每500ms刷新一次OLED (原裸机为每循环全刷) */
        if(++oled_tick >= 5)
        {
            oled_tick = 0;
            OLED_ShowMixedString(1, 1, "温度:");
            OLED_ShowNum(1, 6, temp_int, 2);
            OLED_ShowChar(1, 8, '.');
            OLED_ShowNum(1, 9, temp_deci, 1);
            OLED_ShowMixedString(1, 10, "℃");

            OLED_ShowMixedString(2, 1, "湿度:");
            OLED_ShowNum(2, 6, humi_int, 2);
            OLED_ShowChar(2, 8, '.');
            OLED_ShowNum(2, 9, humi_deci, 1);
            OLED_ShowChar(2, 10, '%');

            OLED_ShowMixedString(3, 1, "气体:");
            OLED_ShowNum(3, 6, (uint16_t)g_env.gas, 4);
            OLED_ShowString(3, 10, "ppm");

            OLED_ShowMixedString(4, 1, "状态:");
            if(g_env.temp > TEMP_MAX || g_env.gas > gas_max || g_env.shake_flag == 1)
            {
                OLED_ShowMixedString(4, 6, "危险");
            }
            else
            {
                OLED_ShowMixedString(4, 6, "正常");
            }

            if(k230_silence <= K230_TIMEOUT_ROUNDS)
            {
                uint8_t h_show = (k230_helmet_cnt > 9) ? 9 : k230_helmet_cnt;
                uint8_t u_show = (k230_head_cnt   > 9) ? 9 : k230_head_cnt;
                OLED_ShowString(4, 11, "H:");
                OLED_ShowNum(4, 13, h_show, 1);
                OLED_ShowString(4, 14, "U:");
                OLED_ShowNum(4, 16, u_show, 1);
            }
            else
            {
                OLED_ShowString(4, 11, "H:-U:-");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ================ 任务: BLE 下行指令 ================ */
static void vBLECmdTask(void *pv)
{
    (void)pv;
    for(;;)
    {
        if(xSemaphoreTake(g_bleCmdSem, portMAX_DELAY) == pdTRUE)
        {
            if(bt24_rx_done)
            {
                BLE_HandleCommand(bt24_rx_buf);
            }
        }
    }
}

/* ================ 中断回调 / 对外接口 ================ */

/* USART2 空闲中断断到一帧后调用 (USART.c 的中断服务程序里) */
void BLE_FrameIsrNotify(void)
{
    BaseType_t hpw = pdFALSE;
    if(g_bleCmdSem != NULL)
    {
        xSemaphoreGiveFromISR(g_bleCmdSem, &hpw);
    }
    portYIELD_FROM_ISR(hpw);
}

void APP_DisableKeyRecal(void)
{
    key0_recal_en = 0;
}

/* FreeRTOS 钩子: 栈溢出/堆耗尽/断言 -> 打印后停机, 便于接调试器 */
static void App_Fatal(const char *tag)
{
    taskDISABLE_INTERRUPTS();
    printf("\r\n[RTOS-FATAL] %s\r\n", tag);
    for(;;);
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    printf("\r\n[RTOS-FATAL] stack overflow: %s\r\n", pcTaskName ? pcTaskName : "?");
    App_Fatal("stack overflow");
}

void vApplicationMallocFailedHook(void)
{
    App_Fatal("malloc failed (heap too small)");
}

void vAssertCalled(const char *pcFile, unsigned long ulLine)
{
    printf("\r\n[RTOS-FATAL] assert %s:%lu\r\n", pcFile, ulLine);
    App_Fatal("assert");
}

/* ================ 创建全部 RTOS 对象与任务 ================ */
void APP_CreateTasks(void)
{
    g_dataMutex = xSemaphoreCreateMutex();
    g_bleCmdSem = xSemaphoreCreateBinary();
    g_sysEvents = xEventGroupCreate();
    configASSERT(g_dataMutex && g_bleCmdSem && g_sysEvents);

    xTaskCreate(vStartupTask, "startup", STACK_STARTUP, NULL, 4, NULL);
    xTaskCreate(vSensorTask,  "sensor",  STACK_SENSOR,  NULL, 4, NULL);
    xTaskCreate(vReportTask,  "report",  STACK_REPORT,  NULL, 3, NULL);
    xTaskCreate(vUITask,      "ui",      STACK_UI,      NULL, 2, NULL);
    xTaskCreate(vBLECmdTask,  "blecmd",  STACK_BLE_CMD, NULL, 3, NULL);
}
