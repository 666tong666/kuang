#include "esp8266.h"
#include "FreeRTOS.h"
#include "task.h"

/* RTOS版: 本文件所有流程只在 vReportTask 任务上下文中运行,
 * 等待一律用 vTaskDelay 让出CPU (delay_ms 忙等会占住CPU) */

//usart3发送和接收数组
uint8_t usart3_txbuf[256];
uint8_t usart3_rxbuf[512];
uint8_t usart3_rxcounter;

char recv_buf[512];
uint8_t cnt = 0,esp8266_cntPre = 0;
uint8_t num = 0;

volatile uint32_t	Rx3Counter	= 0;
volatile uint8_t	Rx3Data 	= 0;
volatile uint8_t	Rx3End 		= 0;
volatile uint8_t	Tx3Buffer[512]={0};
volatile uint8_t	Rx3Buffer[512]={0};

_Bool connect_ack = 1;

/**
 * @brief ESP8266初始化
 * 通过AT指令设置esp8266模式并连接云端
 *
 * @return 没有返回参数

	 ESP8266     AT指令  一堆字符串  长相特殊的字符串

	 4G通讯         ATxxxxx\r\n

	 AT\r\n   测试 AT启动 成功   OK
	 AT+RST\r\n  重启设备      一大串      Ready
	 
	 AT+CWMODE_CUR=1\r\n  设置 wifi模式   1  2  3
	  
 */
void ESP8266_Init(void)
{

	ESP8266_memset_RecvBuff();
	/*退出透传模式*/
	while(ESP8266_SendCmd("+++",""));
	vTaskDelay(pdMS_TO_TICKS(500));
	/*测试ESP8266是否能正常进行AT控制*/
	printf("AT \r\n");
	while(ESP8266_SendCmd("AT\r\n","OK"));
	vTaskDelay(pdMS_TO_TICKS(500));
	/*ESP8266进行重新启动*/
	
	printf("AT+RST \r\n");
	while(ESP8266_SendCmd("AT+RST\r\n",""));
	//while(ESP8266_SendCmd("AT+RESTORE\r\n",""));
	vTaskDelay(pdMS_TO_TICKS(500));
	/*清除数据回传显示输入命令*/
	//printf("ATE0\r\n");
	//while(ESP8266_SendCmd("ATE0\r\n","OK"));
	//Delay_ms(500);
	printf("ESP8266 Init OK\r\n");
	
	
	/*******************以下是连接wifi设备*******************************/
	/*设置ESP8266为STA模式（客户端模式）*/
	printf("AT+CWMODE_CUR\r\n");
	while(ESP8266_SendCmd("AT+CWMODE_CUR=1\r\n", "OK"))
	vTaskDelay(pdMS_TO_TICKS(3000));
	/*设置对应wifi的账号和密码*/
	printf("AT+CWJAP\r\n");
	while(ESP8266_SendCmd(ESP8266_WIFI_INFO, "GOT IP"))
	vTaskDelay(pdMS_TO_TICKS(3000));
	printf("ESP8266 连接WIFI成功！！！\r\n");
	
	/*****************连接云服务器******************************/
	ESP8266_ConnectServer();
	printf("ESP8266 连接云服务器成功！！！\r\n");
	
	/*********************登录操作******************************/
	do{
		ESP8266_memset_RecvBuff();
		MQTT_Connect();
		vTaskDelay(pdMS_TO_TICKS(500));
	/****************通过串口返回判断是否订阅成功  固定的******************/
		if(recv_buf[0] == 0x20 && recv_buf[1] == 0x02)
		{
			printf("登录云服务器的MQTT成功！！！\r\n");
			connect_ack = 0;
		}
	}while(connect_ack != 0);
	connect_ack = 1;

	/*****************订阅云服务操作******************************/
	do{
		ESP8266_memset_RecvBuff();
		
		MQTT_SubscribeTopic();
		vTaskDelay(pdMS_TO_TICKS(500));
		//MQTT_SubscribeTopic_web();
		//Delay_ms(500);
		
		
		/****************通过串口返回判断是否订阅成功******************/
		if((recv_buf[0] == 0x90 && recv_buf[1] == 0x03 && recv_buf[2] == 0x00 && recv_buf[3] == 0x01 && recv_buf[4] == 0x00)||(recv_buf[0] == 0x90 && recv_buf[1] == 0x03 && recv_buf[2] == 0x00 && recv_buf[3] == 0x01 && recv_buf[4] == 0x01))
		{
			printf("订阅MQTT主题成功！！！\r\n");
			connect_ack = 0;
		}
	}while(connect_ack != 0);
	connect_ack = 1;
	/*********************取消订阅********************************/
//	MQTT_UNSubscribeTopic();
	/*****************断开云服务操作******************************/
//	MQTT_Disconnect();
//  printf("MQTT断开服务器成功！！！\r\n");
//	printf("\r\n");
}


/**
 * @brief 清除缓冲区数据内容
 * 
 *
 * @return 没有返回参数
 */
void ESP8266_memset_RecvBuff(void)
{
	//清空发送和接收数组
	memset(usart3_txbuf,0,sizeof(usart3_txbuf));
	memset(usart3_rxbuf,0,sizeof(usart3_rxbuf));
	memset(recv_buf, 0, sizeof(recv_buf));
	cnt = 0;
}


/**
 * @brief ESP8266发送命令
 * 通过串口向ESP8266传输控制命令并核对返回结果是否正确
 *
 * @return 成功返回0，失败返回1
 *

		abc  -> 8266
		8266 -> ok

*/
_Bool ESP8266_SendCmd(char *str, char* rev)
{
	unsigned char timeOut = 200;
	ESP8266_memset_RecvBuff();  //清空变量的缓存

	Usart_SendString(USART3,str);//向esp8266发送指令 ********** 
	while(timeOut--) //超时设置
	{
		if( ESP8266_WaitRecive()==REV_OK)
		{
			if(strstr((const char *)recv_buf, rev) != NULL)	//如果放回结果正确
			{
				ESP8266_memset_RecvBuff();									//清空缓存
				return 0;
			}
		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}
		return 1;
}


/**
 * @brief ESP8266打开透传模式
 * 
 *
 * @return 没有返回结果
 */
void ESP8266_OpenTransmission(void)
{
	//设置透传模式
		memset(usart3_rxbuf,0,sizeof(usart3_rxbuf));    
		while(ESP8266_SendCmd("AT+CIPMODE=1\r\n","OK"))
		vTaskDelay(pdMS_TO_TICKS(500));  
}


/**
 * @brief ESP8266退出透传模式
 * 
 *
 * @return 没有返回结果
 */
void ESP8266_ExitUnvarnishedTrans(void)
{
	while(ESP8266_SendCmd("+++",""))
	vTaskDelay(pdMS_TO_TICKS(500));
}



/**
 * @brief ESP8266连接云服务器
 * 
 *
 * @return 没有返回结果
 */
void ESP8266_ConnectServer(void)
{
	//连接服务器
	printf("AT+CIPSTART");
	while(ESP8266_SendCmd(ESP8266_SERVER,"CONNECT"))
	vTaskDelay(pdMS_TO_TICKS(500));

	//设置透传模式
	ESP8266_OpenTransmission();
	
	//开启发送状态
	printf("AT+CIPSEND"); 
	while(ESP8266_SendCmd("AT+CIPSEND\r\n",">"))
	vTaskDelay(pdMS_TO_TICKS(500));//开始处于透传发送状态

}

/**
 * @brief 串口3的中断回调函数
 * 接收ESP8266发送的消息打印到PC端，并对服务器下发数据进行处理
 *
 * @return 没有返回结果
 */
void USART3_IRQHandler(void)
{
	if(USART_GetITStatus(USART3, USART_IT_RXNE) != RESET)
	{
		//从寄存器中获取接收到的数据
		Rx3Data = USART_ReceiveData(USART3);	
		recv_buf[cnt++] = USART3->DR;   //ESP8266给STM32 返回的内容 
		//清楚接收中断标志位
		USART_ClearITPendingBit(USART3, USART_IT_RXNE);
	}
	/*服务器发送数据时，进行处理*/
	MQTT_RX_DATE_DEAL(recv_buf);
	/* 回显，把接收到的单个字节数据发送到发送方*/
	USART_SendData(USART1, Rx3Data);

}


/**
 * @brief 等待接收到的数据
 * 
 *
 * @return 返回接收完成标志/未完成标志
 */
_Bool ESP8266_WaitRecive(void)
{
	if(cnt == 0) 							//如果接收计数为0 则说明没有处于接收数据中，所以直接跳出，结束函数
		return REV_WAIT;
	if(cnt == esp8266_cntPre)				//如果上一次的值和这次相同，则说明接收完毕
	{
		cnt = 0;							//清0接收计数
		return REV_OK;								//返回接收完成标志
	}
	esp8266_cntPre = cnt;					//置为相同
	return REV_WAIT;								//返回接收未完成标志
}


/**
 * @brief 接收数据处理
 * 
 *
 * @return 没有返回参数
 */
void MQTT_RX_DATE_DEAL(char *buf)   // {"xxxx":"xxx","LED":1}
{
	//printf("iszoulema");
	char *ptr;
	ptr = strstr(buf,"LED");
 // 使用strstr函数找到"LED"的起始位置
  ptr = strstr(buf, "\"LED\"");
  if (ptr != NULL) {
    // 找到了"LED"的起始位置，向后查找":"的位置
    ptr = strchr(ptr, ':');
    if (ptr != NULL) {
      // 向后查找数字的位置
      ptr++;
      while (*ptr == ' ') {
        ptr++;
      }
      // 判断数字是否为0或1
      if (*ptr == '0') {
        printf("LED的值为0\n");
				memset(recv_buf,0,sizeof(recv_buf));
				GPIO_ResetBits(GPIOG, GPIO_Pin_14); //点亮led灯
      } else if (*ptr == '1') {
        printf("LED的值为1\n");
				memset(recv_buf,0,sizeof(recv_buf));
				GPIO_SetBits(GPIOG, GPIO_Pin_14); //熄灭led灯
      } 
    }
  }
		ptr = NULL;
}

/**
 * @brief MQTT链路保活探测 (RTOS版新增)
 * 透传模式下发 PINGREQ(0xC0 0x00), 正常时云端回 PINGRESP(0xD0 0x00),
 * 会作为裸字节出现在 recv_buf 里; 等不到说明 TCP/MQTT 链路已断
 * (WiFi掉线/TCP被服务端关闭/路由器NAT超时等), 调用方应重走接入流程
 *
 * @return 1=链路正常  0=超时无响应(链路断)
 */
uint8_t ESP8266_MQTT_PingCheck(void)
{
	uint16_t t;

	ESP8266_memset_RecvBuff();
	MQTT_SendHeart();                        /* PINGREQ: C0 00 */
	for(t = 0; t < 200; t++)                 /* 最多等2s */
	{
		vTaskDelay(pdMS_TO_TICKS(10));
		if(cnt >= 2 && recv_buf[0] == 0xD0)  /* PINGRESP: D0 00 */
		{
			ESP8266_memset_RecvBuff();
			return 1;
		}
	}
	ESP8266_memset_RecvBuff();
	return 0;
}

