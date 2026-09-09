#include "USART.h"
#include <stdio.h>
#include <string.h>

/* ============================================================
 *  USART2 + DX-BT24 BLE 蓝牙透传模块
 *  接线: STM32 PA2(TX) -> BT24 RXD
 *        STM32 PA3(RX) -> BT24 TXD
 *        STM32 3.3V    -> BT24 VCC
 *        STM32 GND     -> BT24 GND
 *  模块默认: 9600/8/N/1, 未连接时AT命令模式, 连接后自动透传
 * ============================================================ */
volatile uint8_t  bt24_rx_done = 0;
volatile uint16_t bt24_rx_len  = 0;
char bt24_rx_buf[BT24_RX_BUF_SIZE] = {0};

/* 上电第一时间调用: 把 PA2/PA3 钳成上拉输入(串口空闲电平为高)
 * 避免引脚浮空期间电平抖动干扰 DX-BT24 的 RXD */
void BT24_PinsSafe(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_2 | GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}

void USART2_Config(uint32_t baud)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    /* 1. 开时钟: GPIOA(AHB1) + USART2(APB1) */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    /* 2. PA2/PA3 复用为 USART2 */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_USART2);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_USART2);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_2 | GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 3. USART2 基本参数 (与DX-BT24默认一致: 9600 8N1) */
    USART_InitStructure.USART_BaudRate            = baud;
    USART_InitStructure.USART_WordLength           = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits             = USART_StopBits_1;
    USART_InitStructure.USART_Parity               = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                 = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &USART_InitStructure);

    /* 4. 中断: 接收中断 + 空闲中断(用于判断一帧结束) */
    NVIC_InitStructure.NVIC_IRQChannel                   = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 2;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    USART_ITConfig(USART2, USART_IT_IDLE, ENABLE);

    /* 5. 使能 USART2 */
    USART_Cmd(USART2, ENABLE);
}

/* USART2 接收中断: 逐字节入缓冲, 空闲(一帧结束)置完成标志 */
void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) == SET)
    {
        char d = (char)USART_ReceiveData(USART2);
        if (bt24_rx_len < BT24_RX_BUF_SIZE - 1)
        {
            bt24_rx_buf[bt24_rx_len++] = d;
        }
    }
    if (USART_GetITStatus(USART2, USART_IT_IDLE) == SET)
    {
        USART_ReceiveData(USART2);   /* 读DR清除IDLE标志 */
        bt24_rx_buf[bt24_rx_len] = '\0';
        if (bt24_rx_len > 0) bt24_rx_done = 1;
    }
}

/* 蓝牙发送字符串 */
void BT24_SendString(char *str)
{
    Usart_SendString(USART2, str);
}

/* 按小程序协议发送一帧JSON (docs/STM32数据上报协议.md, v2):
 * {"temp":25.0,"humi":60.0,"gas":110,"status":0,"fan":0,"alarm":0}\n
 * 手机连上BT24后, 串口写什么手机notify就收到什么
 *
 * @param temp   温度 ℃
 * @param humi   湿度 %RH
 * @param gas    气体浓度 ppm
 * @param status 0/1（兼容旧协议）；=1 表示存在任一告警
 * @param fan    0/1 当前风扇状态
 * @param alarm  0~7 告警位掩码 (bit0温度 / bit1气体 / bit2震动)
 */
void BT24_SendFrame(float temp, float humi, float gas,
                    uint8_t status, uint8_t fan, uint8_t alarm)
{
    char buf[128];
    int len = snprintf(buf, sizeof(buf),
        "{\"temp\":%.1f,\"humi\":%.1f,\"gas\":%d,\"status\":%d,\"fan\":%d,\"alarm\":%d}\n",
        temp, humi, (int)(gas + 0.5f), status, fan ? 1 : 0, alarm & 0x07);
    if (len > 0)
    {
        Usart_SendBytes(USART2, (uint8_t *)buf, (uint32_t)len);
    }
}

void USART_Config(uint32_t baud)
{
    // 1. 开启USART1时钟 (挂载在APB2)
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
    // 2. 使能GPIOA时钟 (挂载在AHB1)
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    
    GPIO_InitTypeDef mygpio = {0};
    
    // 引脚工作模式: 复用模式
    mygpio.GPIO_Mode = GPIO_Mode_AF;
    // 输出模式: 推挽 PP
    mygpio.GPIO_OType = GPIO_OType_PP;
    // 引脚号
    mygpio.GPIO_Pin = U1_TX | U1_RX;
    // 上拉
    mygpio.GPIO_PuPd = GPIO_PuPd_UP;
    // 引脚速度
    mygpio.GPIO_Speed = GPIO_Speed_100MHz;
    
    GPIO_Init(U1_GROUP, &mygpio);
    
    // 把PA9 和 PA10 复用为 USART1
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_USART1);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_USART1);
    
    USART_InitTypeDef myusart = {0};
    // 波特率
    myusart.USART_BaudRate = baud;
    // 硬件控制流
    myusart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    // 工作模式: 接收和发送
    myusart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    // 校验位
    myusart.USART_Parity = USART_Parity_No;
    // 停止位
    myusart.USART_StopBits = USART_StopBits_1;
    // 字符长度
    myusart.USART_WordLength = USART_WordLength_8b;
    
    USART_Init(USART1, &myusart);
    
    // 配置接收中断
    NVIC_InitTypeDef mynvic = {0};
    mynvic.NVIC_IRQChannel = USART1_IRQn;
    mynvic.NVIC_IRQChannelCmd = ENABLE;
    mynvic.NVIC_IRQChannelPreemptionPriority = 1;
    mynvic.NVIC_IRQChannelSubPriority = 1;
    
    NVIC_Init(&mynvic);
    
    // 开启接收中断
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    // 使能USART1
    USART_Cmd(USART1, ENABLE);
}

// USART1中断变量
char u1_data;
char u1_str[512];
uint16_t len = 0;

void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)
    {
        u1_data = USART_ReceiveData(USART1);
        u1_str[len++] = u1_data;
        
        // 回显测试
        USART_SendData(USART1, u1_data);
    }
}

// 串口3初始化 (使用 PD8-TX, PD9-RX)
void UART3_Config(uint32_t BaudRate)
{
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
    GPIO_InitTypeDef GPIO_InitStructure;

    /* 1. Enable GPIOD clock */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);

    /* 2. Enable USART3 clock (挂载在APB1) */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

    /* 3. Connect USART3 pins to PD8 & PD9 */
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource8, GPIO_AF_USART3);
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource9, GPIO_AF_USART3);

    /* 4. Configure USART3 Tx and Rx as alternate function push-pull */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    /* 5. USART3 basic settings */
    USART_InitStructure.USART_BaudRate = BaudRate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &USART_InitStructure);

    /* 6. NVIC configuration */
    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 7. Enable USART3 */
    USART_Cmd(USART3, ENABLE);
    
    /* 8. Enable the USART3 Receive Interrupt */
    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
}

/***************** 发送指定长度的字节 **********************/
void Usart_SendBytes(USART_TypeDef *pUSARTx, uint8_t *buf, uint32_t len)
{
    uint8_t *p = buf;
    
    while(len--)
    {
        USART_SendData(pUSARTx, *p);
        p++;
        while(USART_GetFlagStatus(pUSARTx, USART_FLAG_TXE) == RESET);
    }
}

/***************** 发送一个字节 **********************/
void Usart_SendByte(USART_TypeDef *pUSARTx, uint8_t ch)
{
    USART_SendData(pUSARTx, ch);
    while(USART_GetFlagStatus(pUSARTx, USART_FLAG_TXE) == RESET);
}

/****************** 发送8位数组 ************************/
void Usart_SendArray(USART_TypeDef *pUSARTx, uint8_t *array, uint16_t num)
{
    uint16_t i;
    for(i = 0; i < num; i++)
    {
        Usart_SendByte(pUSARTx, array[i]);    
    }
    while(USART_GetFlagStatus(pUSARTx, USART_FLAG_TC) == RESET);
}

/***************** 发送字符串 **********************/
void Usart_SendString(USART_TypeDef *pUSARTx, char *str)
{
    unsigned int k = 0;
    do
    {
        Usart_SendByte(pUSARTx, *(str + k));
        k++;
    } while(*(str + k) != '\0');

    while(USART_GetFlagStatus(pUSARTx, USART_FLAG_TC) == RESET);
}

void Send_Byte(USART_TypeDef *USARTx, uint16_t Data)
{
    USART_SendData(USARTx, Data);
    while(USART_GetFlagStatus(USARTx, USART_FLAG_TXE) != SET);
}

void Send_Str(USART_TypeDef *USARTx, char *str)
{
    do {
        Send_Byte(USARTx, *str);
    } while(*(str++) != '\0');
    
    while(USART_GetFlagStatus(USARTx, USART_FLAG_TC) != SET);
}

// printf重定向到USART1
int fputc(int ch, FILE *f)
{
    USART_SendData(USART1, (uint8_t)ch);
    while(USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
    return ch;
}

/***************** 发送一个16位数 **********************/
void Usart_SendHalfWord(USART_TypeDef *pUSARTx, uint16_t ch)
{
    uint8_t temp_h, temp_l;

    temp_h = (ch & 0xFF00) >> 8;
    temp_l = ch & 0xFF;

    USART_SendData(pUSARTx, temp_h);    
    while(USART_GetFlagStatus(pUSARTx, USART_FLAG_TXE) == RESET);

    USART_SendData(pUSARTx, temp_l);    
    while(USART_GetFlagStatus(pUSARTx, USART_FLAG_TXE) == RESET);    
}