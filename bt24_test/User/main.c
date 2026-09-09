/**
 * ============================================================================
 *  DX-BT24 蓝牙模块测试工程 (STM32F407VET6, 标准外设库)
 * ============================================================================
 *  串口分配:
 *    USART1 (PA9-TX / PA10-RX) 115200 —— 调试口, 接 USB-TTL 看输出/发指令
 *    USART2 (PA2-TX / PA3-RX)  9600  —— 接 DX-BT24 (PA2->RXD, PA3<-TXD)
 *
 *  接线:
 *    BT24 RXD <- PA2      BT24 TXD -> PA3
 *    BT24 VCC -> 3.3V     BT24 GND -> GND
 *    USB-TTL RX <- PA9    USB-TTL TX -> PA10 (共地)
 *    调试 LED: PG13 (低电平点亮), 500ms 心跳
 *
 *  测试功能:
 *    1. 每 3 秒向 BT24 发一次 "AT+NAME", 模块正常应回 "+NAME=<名字> OK"
 *       -> 蓝牙广播名直接打印在调试串口 (注意: AT 指令仅在蓝牙未被手机连接时有效)
 *    2. 上电时额外查询一次 MAC 地址 (AT+LADDR), 便于对照手机扫描列表
 *    3. 每 2 秒向 BT24 发 "BT24 TEST n" 测试帧 —— 手机 BLE 连上后
 *       (DX-SMART / nRF Connect / 小程序) 应能透传收到
 *    4. 在调试串口助手里输入的任何字符, 实时转发给 BT24
 *       (可手动发 AT 指令: 记得勾选"发送新行")
 *    5. BT24 的所有回显打印到调试口, 加 [BT24] 前缀
 *
 *  判定:
 *    - 串口助手能看到 [BT24] +OK        -> 模块串口通路正常, 固件正常
 *    - 手机能连上并收到 TEST 帧         -> 射频正常, 整链路 OK
 *    - AT 无回复但手机能收帧            -> 模块固件异常(半坏), 建议换
 *    - AT 无回复且手机连不上            -> 模块损坏或接线/供电问题
 * ============================================================================
 */
#include "stm32f4xx.h"
#include <stdio.h>

/* ---------------- 配置 ---------------- */
#define DEBUG_BAUD    115200
#define BT24_BAUD     9600        /* DX-BT24 出厂默认 */

/* ---------------- 全局状态 ---------------- */
static volatile uint32_t  g_ms = 0;              /* SysTick 毫秒计数 */

static volatile char      bt_line[128];          /* BT24 -> 调试口 行缓冲 */
static volatile uint16_t  bt_len  = 0;
static volatile uint8_t   bt_busy = 0;           /* 正在接收一帧 */
static volatile uint32_t  bt_last_ms = 0;

/* ============================================================================
 *  SysTick 毫秒时基
 * ==========================================================================*/
void SysTick_Handler(void)
{
    g_ms++;
}

static void delay_ms(uint32_t ms)
{
    uint32_t t0 = g_ms;
    while ((g_ms - t0) < ms) { }
}

/* ============================================================================
 *  GPIO: LED (PG13, 低电平点亮) + BT24 引脚上电保护
 * ==========================================================================*/
static void LED_Init(void)
{
    GPIO_InitTypeDef g;
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOG, ENABLE);
    g.GPIO_Pin   = GPIO_Pin_13;
    g.GPIO_Mode  = GPIO_Mode_OUT;
    g.GPIO_OType = GPIO_OType_PP;
    g.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOG, &g);
    GPIO_SetBits(GPIOG, GPIO_Pin_13);            /* 灭 */
}

/* 上电第一时间调用: PA2/PA3 钳为上拉输入, 避免浮空抖动干扰 BT24 */
static void BT24_PinsSafe(void)
{
    GPIO_InitTypeDef g;
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    g.GPIO_Pin  = GPIO_Pin_2 | GPIO_Pin_3;
    g.GPIO_Mode = GPIO_Mode_IN;
    g.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &g);
}

/* ============================================================================
 *  USART1 调试口 115200 (PA9-TX / PA10-RX)
 * ==========================================================================*/
static void Debug_UART_Init(uint32_t baud)
{
    GPIO_InitTypeDef  g;
    USART_InitTypeDef u;
    NVIC_InitTypeDef  n;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);

    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9,  GPIO_AF_USART1);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_USART1);
    g.GPIO_Pin   = GPIO_Pin_9 | GPIO_Pin_10;
    g.GPIO_Mode  = GPIO_Mode_AF;
    g.GPIO_OType = GPIO_OType_PP;
    g.GPIO_PuPd  = GPIO_PuPd_UP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &g);

    u.USART_BaudRate            = baud;
    u.USART_WordLength           = USART_WordLength_8b;
    u.USART_StopBits             = USART_StopBits_1;
    u.USART_Parity               = USART_Parity_No;
    u.USART_HardwareFlowControl  = USART_HardwareFlowControl_None;
    u.USART_Mode                 = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &u);

    /* 开接收中断: 调试口输入的字符实时转发给 BT24 */
    n.NVIC_IRQChannel                   = USART1_IRQn;
    n.NVIC_IRQChannelPreemptionPriority = 1;
    n.NVIC_IRQChannelSubPriority        = 1;
    n.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&n);
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    USART_Cmd(USART1, ENABLE);
}

/* printf 重定向 (工程已开 MicroLIB) */
int fputc(int ch, FILE *f)
{
    USART_SendData(USART1, (uint8_t)ch);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) { }
    return ch;
}

/* ============================================================================
 *  USART2 蓝牙口 9600 (PA2-TX / PA3-RX)
 * ==========================================================================*/
static void BT24_UART_Init(uint32_t baud)
{
    GPIO_InitTypeDef  g;
    USART_InitTypeDef u;
    NVIC_InitTypeDef  n;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_USART2);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_USART2);
    g.GPIO_Pin   = GPIO_Pin_2 | GPIO_Pin_3;
    g.GPIO_Mode  = GPIO_Mode_AF;
    g.GPIO_OType = GPIO_OType_PP;
    g.GPIO_PuPd  = GPIO_PuPd_UP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &g);

    u.USART_BaudRate            = baud;
    u.USART_WordLength           = USART_WordLength_8b;
    u.USART_StopBits             = USART_StopBits_1;
    u.USART_Parity               = USART_Parity_No;
    u.USART_HardwareFlowControl  = USART_HardwareFlowControl_None;
    u.USART_Mode                 = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &u);

    n.NVIC_IRQChannel                   = USART2_IRQn;
    n.NVIC_IRQChannelPreemptionPriority = 1;
    n.NVIC_IRQChannelSubPriority        = 2;
    n.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&n);
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    USART_ITConfig(USART2, USART_IT_IDLE, ENABLE);

    USART_Cmd(USART2, ENABLE);
}

/* 阻塞发送字符串到 BT24 */
static void BT24_SendString(const char *s)
{
    while (*s)
    {
        USART_SendData(USART2, (uint8_t)*s++);
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) { }
    }
}

/* ============================================================================
 *  中断: 调试口字符 -> 转发 BT24 ; BT24 回复 -> 行缓冲
 * ==========================================================================*/
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)
    {
        char d = (char)USART_ReceiveData(USART1);
        /* 转发给蓝牙, 并本地回显(带括号便于区分) */
        USART_SendData(USART2, (uint8_t)d);
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) { }
        USART_SendData(USART1, (uint8_t)d);
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) { }
    }
}

void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) == SET)
    {
        char d = (char)USART_ReceiveData(USART2);
        bt_last_ms = g_ms;
        bt_busy    = 1;
        if (bt_len < sizeof(bt_line) - 1)
        {
            bt_line[bt_len++] = d;
            if (d == '\n')                     /* 一行结束 */
            {
                bt_line[bt_len] = '\0';
                bt_busy = 0;
                /* 直接置 ready 标志: 用 bt_len>0 且 !bt_busy 表示有整帧 */
            }
        }
    }
    if (USART_GetITStatus(USART2, USART_IT_IDLE) == SET)
    {
        USART_ReceiveData(USART2);             /* 读DR清除IDLE标志 */
        bt_busy = 0;                           /* 空闲 = 一帧结束 */
    }
}

/* ============================================================================
 *  main
 * ==========================================================================*/
int main(void)
{
    uint32_t t_led = 0, t_at = 0, t_tx = 0;
    uint32_t tx_cnt = 0;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    BT24_PinsSafe();                 /* 先钳住 PA2/PA3 */
    SysTick_Config(SystemCoreClock / 1000);
    LED_Init();
    Debug_UART_Init(DEBUG_BAUD);

    delay_ms(1500);                  /* 等 BT24 完成上电初始化 */
    BT24_UART_Init(BT24_BAUD);

    printf("\r\n\r\n===== DX-BT24 TEST FIRMWARE (STM32F407VET6) =====\r\n");
    printf("[SYS ] debug: USART1 PA9/PA10 @%d\r\n", DEBUG_BAUD);
    printf("[SYS ] BT24 : USART2 PA2/PA3  @%d\r\n", BT24_BAUD);
    printf("[SYS ] 1) auto query name \"AT+NAME\" every 3s -> prints [BT24] +NAME=xxx\r\n");
    printf("[SYS ] 2) auto send TEST frame every 2s for phone\r\n");
    printf("[SYS ] 3) chars typed here are forwarded to BT24\r\n");
    printf("[SYS ] LED PG13 blinks = firmware alive\r\n\r\n");

    /* 上电先查一次 MAC 地址, 便于手机扫描列表对照无名设备 */
    printf("[SYS ] query MAC ...\r\n");
    BT24_SendString("AT+LADDR\r\n");
    delay_ms(500);
    printf("[SYS ] query NAME ...\r\n");
    BT24_SendString("AT+NAME\r\n");

    while (1)
    {
        /* --- 打印 BT24 的回复 (整行或 100ms 静默即输出) --- */
        if (bt_len > 0 && bt_busy == 0)
        {
            bt_line[bt_len] = '\0';
            printf("[BT24] %s", (char *)bt_line);
            if (bt_line[bt_len - 1] != '\n') printf("\r\n");
            bt_len = 0;
        }
        else if (bt_len > 0 && bt_busy && (g_ms - bt_last_ms) > 100)
        {
            bt_busy = 0;             /* 超时强制成帧, 下轮打印 */
        }

        /* --- LED 心跳 500ms --- */
        if ((g_ms - t_led) >= 500)
        {
            t_led = g_ms;
            GPIO_ToggleBits(GPIOG, GPIO_Pin_13);
        }

        /* --- 每 3s 查一次蓝牙名字 (回复打印为 [BT24] +NAME=xxx) --- */
        if ((g_ms - t_at) >= 3000)
        {
            t_at = g_ms;
            printf("[SYS ] -- query name --\r\n");
            BT24_SendString("AT+NAME\r\n");
        }

        /* --- 每 2s 发透传测试帧 --- */
        if ((g_ms - t_tx) >= 2000)
        {
            t_tx = g_ms;
            tx_cnt++;
            printf("[SYS ] tx frame #%d\r\n", (unsigned)tx_cnt);
            BT24_SendString("BT24 TEST ");
            /* 简单十进制输出计数 */
            char numbuf[12];
            uint32_t v = tx_cnt, i = 0;
            if (v == 0) numbuf[i++] = '0';
            while (v) { numbuf[i++] = '0' + (v % 10); v /= 10; }
            while (i)  { USART_SendData(USART2, numbuf[--i]); while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) { } }
            BT24_SendString("\r\n");
        }
    }
}
