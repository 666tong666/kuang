#include "stm32f4xx.h"
#include "LED.h"
#include "Delay.h"
#include "KEY.h"
#include "EXTI.h"
#include "USART.h"
#include <stdio.h>
#include <math.h>
#include "BEEP.h"
#include "DHT11.h"
#include "oled.h"
#include "mq135.h"
#include "mpu6050.h"
#include "mqtt.h"
#include "esp8266.h"
#include <stdlib.h>
#include <string.h>
#include "green.h"
#include "motor.h"
#include "fan.h"

// 外部全局变量声明
extern uint8_t humi_int;
extern uint8_t humi_deci;
extern uint8_t temp_int;
extern uint8_t temp_deci;
extern uint8_t dht_err; 
extern int16_t gx_off, gy_off, gz_off;

#define RL      10.0f    // 负载电阻 kΩ
#define R0      12.50f

/* MQ135校准参数: 干净空气中 Rs/R0 基准比值(3.4对应清洁空气约5ppm等效值,
 * 远低于50ppm报警线; 相对浓度眮Ｈ园磀atasheet曲线斜率响应) */
#define MQ135_CLEAN_RS_RATIO  3.4f

/* MQ135预热校准: 实测该传感器预热约1分钟后读数已无漂移趋势, 只剩
 * ±10%短时噪声(再长的"稳定检测"只会被噪声反复打断), 故用固定预热时长,
 * 校准时用6.4s长窗口平均压制波动(新传感器首次使用建议先通电老化数小时) */
#define MQ135_WARMUP_S        120

/*阈值配置*/
#define TEMP_MAX       32      // 温度上限 ℃
float gas_max = 50.0f;         // 气体等效ppm上限(可由小程序经BLE下发修改)

/* 风扇状态跟随系统告警：任一超标(温度/气体)即显示开启, 全部恢复正常才关闭
 * 注：用户当前未接物理风扇, 该状态仅用于小程序联动显示 */

// 震动检测参数
/* 阈值含义: 相邻两次采样(约100ms)间的加速度变化量, 单位LSB。
 * ACCEL_CONFIG为±2g量程(16384 LSB/g), 8192 LSB = 0.5g ——
 * 手碰/挪动/旋转以及桌面传导的冲击余振(通常<0.4g)都不再误报,
 * 直接摇晃/敲击设备(>0.5g)仍可靠触发; 若误触发, 看串口
 * "!!!VIBRATION DETECT!!! deltaAX..." 打印的实测值再微调阈值 */
uint16_t g_shake_threshold = 8192;
#define SHAKE_CNT         3        // 连续N次超限确认震动
#define SHAKE_RELEASE_CNT 5        // 震动消失后再等5个周期解除锁存

/* 震动上报粘性标志: 检测到震动即置1, 由5s周期上报流程读走后清0。
 * 解决锁存仅维持约0.5s、短震动落在两次上报之间被漏掉的问题 */
uint8_t shake_pending = 0;

/* MPU6050 串口诊断开关: 1=每100ms打印原始值/差值/状态(排查误触发用),
 * 排查完改0关闭。输出约600字节/秒, 115200波特率无压力 */
#define MPU_DEBUG_PRINT 1

/**
 * @brief  MPU6050震动检测函数
 * @param  ax,ay,az 当前原始加速度计数值
 * @retval 1:检测到震动(锁存)  0:无震动
 */
uint8_t MPU6050_VibrationDetect(int16_t ax, int16_t ay, int16_t az)
{
    static int16_t ax_last = 0;
    static int16_t ay_last = 0;
    static int16_t az_last = 0;
    static uint8_t cnt = 0;
    static uint8_t vib_latch = 0;
    static uint8_t release_timer = 0;

    // IIC读取出错爆值过滤
    if(abs(ax) > 32000 || abs(ay) > 32000 || abs(az) > 32000)
    {
        ax = ax_last;
        ay = ay_last;
        az = az_last;
    }

    // 计算三轴眮２钪?
    int16_t delta_ax = abs(ax - ax_last);
    int16_t delta_ay = abs(ay - ay_last);
    int16_t delta_az = abs(az - az_last);

    // 更新上一次数值
    ax_last = ax;
    ay_last = ay;
    az_last = az;

    // 判断震动，使用全局可修改阈值g_shake_threshold
    if(delta_ax > g_shake_threshold || delta_ay > g_shake_threshold || delta_az > g_shake_threshold)
    {
        cnt++;
        release_timer = 0;
#if MPU_DEBUG_PRINT
        /* 打印每个超限采样: 看差值多大、是否连续凑满SHAKE_CNT */
        printf("[MPU] over! dx=%d dy=%d dz=%d (thr=%u) cnt=%d/%d\r\n",
               delta_ax, delta_ay, delta_az, g_shake_threshold, cnt, SHAKE_CNT);
#endif
        if(cnt >= SHAKE_CNT)
        {
            if(vib_latch == 0)
            {
                printf("[MPU] !!! SHAKE LATCH ON !!!\r\n");
            }
            vib_latch = 1;
            shake_pending = 1;    /* 粘性置位, 直到被上报流程取走 */
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
                shake_pending = 0;   /* 事件已结束: 若粘性标志还没被上报取走,
                                        说明它与上一帧属同一次震动, 丢弃之,
                                        以免拖慢"恢复正常"帧的发出 */
#if MPU_DEBUG_PRINT
                printf("[MPU] shake latch released\r\n");
#endif
            }
        }
    }
    return vib_latch;
}

/* ================ MQ135 校准与补偿 ================ */

/* 校准后的R0(kΩ), 上电自动校准, KEY0长按可随时重校 */
static float gas_r0 = R0;
static uint8_t  key0_recal_en = 1;   /* KEY0重校功能开关(上电检测PG2异常时自动关闭) */
static uint16_t key0_hold_ms  = 0;   /* KEY0按住时长累计 */

/* 温湿度修正系数(datasheet曲线条件为20℃/33%RH):
 * Rs_corrected = Rs / cf, 系数来自MQ135通用线性化模型 */
static float MQ135_CorrectionFactor(float t, float h)
{
    if(t <= 0.0f && h <= 0.0f) return 1.0f;   /* DHT11尚未读到数据, 不修正 */
    if(t < 20.0f)
        return 0.00035f*t*t - 0.02718f*t + 1.39538f - (h - 33.0f)*0.0018f;
    return -0.003333333f*t - 0.001923077f*h + 1.130128205f;
}

/* 在当前环境采样并校准R0 —— 必须在干净空气中调用!
 * 连续采32次(约0.7s)取平均作为清洁空气基线 */
static void MQ135_Calibrate(void)
{
    uint32_t sum = 0;
    uint8_t  k;
    uint16_t adc;
    float    Rs;

    for(k = 0; k < 64; k++)
    {
        sum += MQ135_Get_ADC();
        delay_ms(100);                     /* 6.4s长窗口, 压制±10%短时波动 */
    }
    adc = (uint16_t)(sum / 64);
    if(adc < 10) adc = 10;                    /* 防除零 */
    Rs = RL * (4095.0f / (float)adc - 1.0f);
    gas_r0 = Rs / MQ135_CLEAN_RS_RATIO;
    printf("MQ135 cal: adc=%d Rs=%.2fk R0=%.2fk\r\n", adc, Rs, gas_r0);

    /* 基线合法性检查: adc贴顶/贴底说明硬件异常, 保留默认R0并打印排查方向 */
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

/* MQ135预热+校准: 固定MQ135_WARMUP_S秒预热后取清洁空气基线 */
static void MQ135_WarmupAndCalibrate(void)
{
    uint16_t waited_s = 0;

    printf("MQ135 warming up %ds, keep clean air...\r\n", MQ135_WARMUP_S);

    while(waited_s < MQ135_WARMUP_S)
    {
        delay_ms(1000);
        waited_s++;

        /* OLED: 第3行显示预热倒计时(秒) */
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
}

/* ================ BLE 下行指令处理 ================ */

/* 处理小程序经蓝牙透传下发的JSON指令(帧尾\n, 由USART2空闲中断断帧):
 *   {"cmd":"gas_max","val":80}\n  -> 修改气体笣鱼兄?
 *   成功回 {"cmd":"gas_max","ack":80}\n 给手机, 同时打印到调试串口 */
static void BLE_HandleCommand(char *buf)
{
    char *p;

    if(strstr(buf, "gas_max") != NULL)
    {
        p = strstr(buf, "val");
        if(p != NULL)
        {
            p = strchr(p, ':');                 /* 找到 val 后面的冒号 */
            if(p != NULL)
            {
                float val = strtof(p + 1, NULL); /* 解析 ":80}" 中的 80 */
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

    bt24_rx_len  = 0;      /* 清空缓冲, 准备接收下一帧 */
    bt24_rx_buf[0] = '\0';
    bt24_rx_done  = 0;
}

int main(void)
{
    int16_t ax, ay, az, gx, gy, gz;
    float nh3_ppm;
    uint16_t adc_val;
    uint8_t dht_tick = 0;
    uint8_t shake_flag = 0;
	static uint16_t mqtt_cnt = 0;
	static uint8_t last_alarm_sent = 0;   /* 最近一帧上报的告警位掩码, 用于事件驱动补发 */

    // 1.系统基础配置
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    BT24_PinsSafe();          /* 最先钳住PA2/PA3, 避免上电电平跳变干扰蓝牙模块 */
	delay_ms(2000);
    USART2_Config(BT24_BAUD);   /* PA2/PA3 -> DX-BT24 蓝牙, 默认9600 */
    USART_Config(115200);
    printf("System Starting...\r\n");
	
    LED_Init();
    KEY_Config();
    Fan_Init();   /* 散热风扇：默认关闭，温度自动控制 */
    /* 上电检测KEY0(PG2): 内部上拉正常应读高。若上电即?(按键卡死/引脚被拉低/
     * 板子按键接法不是低有效), 自动关闭按键重校功能, 避免主循环死等松手卡死 */
    if(GPIO_ReadInputDataBit(KEY_GROUP, KEY0) == 0)
    {
        key0_recal_en = 0;
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
    OLED_Clear();
	ESP8266_Init();
	
    ADC_MQ135_Init();
    MPU6050_Init();
    delay_ms(200);
    MPU6050_CalibrateGyro();

    /* MQ135预热+校准: 等读数稳定后自动取清洁空气基线(60~300s) */
    OLED_Clear();
    OLED_ShowString(1, 1, "MQ135 warming up");
    OLED_ShowString(2, 1, "keep clean air!");
    MQ135_WarmupAndCalibrate();
    OLED_Clear();

    printf("Initialization Complete!\r\n");

    while(1)
    {
		mqtt_cnt++;
        if(k230_silence < 60000) k230_silence++;   /* K230接收中断清0; 用于离线判断 */
        // --- 1. 读取传感器数据 ---
        
        // DHT11读取 (约每200ms读一次)
        dht_tick++;
        if(dht_tick >= 2)
        {
            dht_tick = 0;
            DHT11_Read_Data();
        }

        // MPU6050 读取三轴加速度并调用你的震动检测算法
        MPU6050_ReadAll(&ax, &ay, &az, &gx, &gy, &gz);
        shake_flag = MPU6050_VibrationDetect(ax, ay, az);
#if MPU_DEBUG_PRINT
        /* 原始值诊断: 平放静止时 az约±16384(1g重力), ax/ay接近0;
         * 若数值无外部触碰却大幅跳变, 多为I2C读数毛刺或供电干扰 */
        printf("[MPU] ax=%d ay=%d az=%d lat=%d pend=%d\r\n",
               ax, ay, az, shake_flag, shake_pending);
#endif

        // MQ135 气体浓度计算: 16次均值滤波 + 温湿度修正 + 校准后的R0
        adc_val = MQ135_Get_ADC_Avg(16);
        if(adc_val < 10) adc_val = 10;              /* 防除零 */
        if(gas_r0 < 0.01f) gas_r0 = R0;             /* R0异常时兜底, 防除零出NaN */
        {
            float temp_now = temp_int + temp_deci / 10.0f;
            float humi_now = humi_int + humi_deci / 10.0f;
            float Rs = RL * (4095.0f / (float)adc_val - 1.0f);
            Rs /= MQ135_CorrectionFactor(temp_now, humi_now);   /* 折算回20℃/33%RH标准条件 */
            float rs_r0 = Rs / gas_r0;
            if(rs_r0 < 0.01f) rs_r0 = 0.01f;
            nh3_ppm = 102.2f * powf(rs_r0, -2.473f);
            if(nh3_ppm < 1.0f)   nh3_ppm = 1.0f;
            if(nh3_ppm > 9999.0f) nh3_ppm = 9999.0f;
            printf("ADC:%d Rs:%.2f R0:%.2f PPM:%.1f\r\n", adc_val, Rs, gas_r0, nh3_ppm);
        }

        /* --- BLE下行指令: 小程序发来的阈值修改等命令 --- */
        if(bt24_rx_done)
        {
            BLE_HandleCommand(bt24_rx_buf);
        }

        /* KEY0(PG2)长按1.5s: 在干净空气中重新校准R0
         * 长按防误触; 等松手带3s超时, 引脚被拉低也不会卡死 */
        if(key0_recal_en && GPIO_ReadInputDataBit(KEY_GROUP, KEY0) == 0)
        {
            key0_hold_ms += 100;                     /* 主循环一轮100ms */
            if(key0_hold_ms >= 1500)
            {
                key0_hold_ms = 0;
                BEEP_Beep(100);
                printf("MQ135 recalibrating...\r\n");
                MQ135_Calibrate();
                BEEP_Beep(100);
                uint16_t guard = 0;
                while(GPIO_ReadInputDataBit(KEY_GROUP, KEY0) == 0 && guard < 30)
                {
                    delay_ms(100);                   /* 最多等3秒 */
                    guard++;
                }
            }
        }
        else
        {
            key0_hold_ms = 0;
        }
		
		/* 事件驱动上报: 告警状态(温度/气体/震动)变化时, 距上一帧≥200ms就补发,
		 * 让小程序端告警出现与恢复都在1~2秒内可见, 不必等下一个5s周期 */
		{
			uint8_t now_alarm = 0;
			if (temp_int + temp_deci / 10.0f > TEMP_MAX) now_alarm |= 0x01;
			if (nh3_ppm > gas_max)                       now_alarm |= 0x02;
			if (shake_flag || shake_pending)             now_alarm |= 0x04;
			if (now_alarm != last_alarm_sent && mqtt_cnt >= 2)
			{
				mqtt_cnt = 5;    /* 强制触发下方上报块 */
			}
		}

		if(mqtt_cnt >= 5)   //50*100ms =5s上报一次
		{
			mqtt_cnt = 0;
			float temp_val = temp_int + temp_deci / 10.0f;
			float humi_val = humi_int + humi_deci / 10.0f;
			float gas_val  = nh3_ppm;
			/* 震动取"瞬时锁存 | 上报期粘性标志", 取走即清:
			 * 确保两次上报之间发生过的短震动也会随本帧发出 */
			uint8_t shake_report = shake_flag | shake_pending;
			shake_pending = 0;
			/* 兼容旧字段: 任一告警即 status=1 */
			uint8_t status = (temp_val > TEMP_MAX || gas_val > gas_max || shake_report) ? 1 : 0;
			/* v2 协议: 告警位掩码（每类独立置位） */
			uint8_t alarm = 0;
			if (temp_val > TEMP_MAX)   alarm |= 0x01;  /* bit0 温度过高 */
			if (gas_val  > gas_max)    alarm |= 0x02;  /* bit1 气体超标 */
			if (shake_report)          alarm |= 0x04;  /* bit2 震动告警 */
			last_alarm_sent = alarm;   /* 记录已发状态, 供事件驱动比较 */
			/* 风扇状态跟随告警：温度或气体任一超标 -> 开(显示), 否则关 */
			uint8_t fan = (temp_val > TEMP_MAX || gas_val > gas_max) ? 1 : 0;
			if (fan) Fan_On(); else Fan_Off();
			/* K230 安全帽人数 (UART4中断更新; K230离线时冻结在最后一帧数值,
			 * 是否离线看OLED第4行的 H:-U:- 提示) */
			uint8_t helmet = k230_helmet_cnt;
			uint8_t head   = k230_head_cnt;
			/* MQTT上报 (v2.1: +helmet+head) */
			MQTT_PublicTopic(humi_val, temp_val, gas_val, fan, alarm, helmet, head);
			printf("MQTT upload: temp=%.1f humi=%.1f gas=%.1f fan=%u alarm=%u helmet=%u head=%u\r\n",
			       temp_val, humi_val, gas_val, fan, alarm, helmet, head);
			/* BLE透传 (v2.1: +helmet+head) */
			BT24_SendFrame(temp_val, humi_val, gas_val, status, fan, alarm, helmet, head);
		}

        // 当前温度浮点数（方便比较）
        float current_temp = temp_int + temp_deci / 10.0f;


        // ==================== 2. 阈值判断与报警逻辑（你可以在这里写发生什么） ====================
        
        if (current_temp > TEMP_MAX || nh3_ppm > gas_max || shake_flag == 1)
        {
            // === 【超标/危险情况】 ===
            // ?? 在这里写你希望发生的事趋侠磺蜂鸣器响、电机转、LED闪烁等）
            LED_Off();
			BEEP_On();
        }
        else
        {
            // === 【一切正常情况】 ===
            // ?? 在这里写恢复正常时的事趋侠磺关闭报警、停止电机等）
            LED_On();
			BEEP_Off();
        }
        // ==================================================================================


        // --- 3. 刷新 OLED 显示 ---

        // 第1行：温度（中文标签 + 实时数值）
        OLED_ShowMixedString(1, 1, "温度:");           /* 温/度/: = col 1~5 */
        OLED_ShowNum(1, 6, temp_int, 2);               /* col 6~7 */
        OLED_ShowChar(1, 8, '.');
        OLED_ShowNum(1, 9, temp_deci, 1);               /* col 9 */
        OLED_ShowMixedString(1, 10, "℃");              /* ℃ = col 10~11 */

        // 第2行：湿度
        OLED_ShowMixedString(2, 1, "湿度:");
        OLED_ShowNum(2, 6, humi_int, 2);
        OLED_ShowChar(2, 8, '.');
        OLED_ShowNum(2, 9, humi_deci, 1);
        OLED_ShowChar(2, 10, '%');

        // 第3行：气体浓度 (ppm)
        OLED_ShowMixedString(3, 1, "气体:");            /* 气/体/: = col 1~5 */
        OLED_ShowNum(3, 6, (uint16_t)nh3_ppm, 4);      /* 4 位数字, 前导 0 */
        OLED_ShowString(3, 10, "ppm");                 /* col 10~12 */

        // 第4行：状态 (正常 / 危险)
        OLED_ShowMixedString(4, 1, "状态:");
        if(current_temp > TEMP_MAX || nh3_ppm > gas_max || shake_flag == 1)
        {
            OLED_ShowMixedString(4, 6, "危险");         /* 危/险 = col 6~9 */
        }
        else
        {
            OLED_ShowMixedString(4, 6, "正常");         /* 正/常 = col 6~9 */
        }

        // 第4行右侧: K230安全帽人数 H:戴帽 U:未戴 (col 11~16, 离线显示--)
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

        // 循环延时 100ms
        delay_ms(100);
    }
}