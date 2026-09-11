#ifndef __ESP8266_H
#define __ESP8266_H


#include "stm32f4xx.h"
#include "Delay.h"
#include "mqtt.h"


#include <stdio.h>
#include <string.h>
#include <stdarg.h>


#define REV_OK		0	//接收完成标志
#define REV_WAIT	1	//接收未完成标志


#define ESP8266_WIFI_INFO		"AT+CWJAP=\"REDMI K90 Ultra\",\"tongtong\"\r\n" //wifi的账号和密码

//#define ESP8266_SERVER "AT+CIPSTART=\"TCP\",\"mqtts.heclouds.com\",1883\r\n"  //(TCP/UDP),模式ONENET Borker Address，Borker Port
#define ESP8266_SERVER "AT+CIPSTART=\"TCP\",\"01e46df499.st1.iotda-device.cn-north-4.myhuaweicloud.com\",1883\r\n"  //(TCP/UDP)模式，ALIYUN Borker Address，Borker Port


extern uint8_t cnt ;
extern uint8_t esp8266_cntPre ;
extern uint8_t num ;



_Bool ESP8266_WaitRecive(void);
void ESP8266_Init(void);
void ESP8266_memset_RecvBuff(void);
_Bool ESP8266_SendCmd(char *str, char* rev);
void ESP8266_ConnectServer(void);
uint8_t ESP8266_MQTT_PingCheck(void);   /* MQTT链路保活探测: 1正常 0断线 */
void recv_data_control(char * data);
void MQTT_RX_DATE_DEAL(char *recv_buf);
#endif
