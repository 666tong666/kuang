#include "Delay.h"

/* ============================================================
 * 延时驱动 (FreeRTOS 版)
 * 原版用 SysTick 忙等实现 us/ms 延时 —— 与 FreeRTOS 的 SysTick
 * 节拍冲突, 调度器启动后严禁使用。改为 DWT CYCCNT 周期计数:
 *   - 不占用任何定时器外设, 调度器启动前后均可用
 *   - 纯忙等, 可在中断/临界区内安全使用 (DHT11 时序依赖)
 * 任务内的长延时请使用 vTaskDelay(), 不要用本文件接口占着 CPU
 * ============================================================ */

void DWT_Init(void)
{
    /* 使能 DWT 计数器: DEMCR.TRCENA + CYCCNT 使能 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void Delay(volatile uint32_t cnt)
{
    while (cnt > 0){
        cnt--;
    }
}

/* 微秒级忙等 (DWT 周期差值计算, 自动处理 32 位回绕) */
void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000u);

    while ((DWT->CYCCNT - start) < ticks);
}

/* 毫秒级忙等 (任务上下文的长延时请改用 vTaskDelay) */
void delay_ms(uint32_t ms)
{
    while (ms--)
    {
        delay_us(1000);
    }
}

void delay_s(uint32_t s)
{
    while(s--)
    {
        delay_ms(500);
        delay_ms(500);
    }
}
