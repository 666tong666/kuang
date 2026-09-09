#ifndef __MQTT_H
#define __MQTT_H
#include "esp8266.h"
#include "DELAY.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "USART.h"
#define BYTE0(dwTemp)       (*( char *)(&dwTemp))
#define BYTE1(dwTemp)       (*((char *)(&dwTemp) + 1))
#define BYTE2(dwTemp)       (*((char *)(&dwTemp) + 2))
#define BYTE3(dwTemp)       (*((char *)(&dwTemp) + 3))
	
/************************	ONENET服务器***************************************/
//#define MQTT_Client_ID  "DHT11" //设备名字
//#define MQTT_User_Name "G7NqVv6le8" //设备ID
//#define MQTT_Password  "version=2018-10-31&res=products%2FG7NqVv6le8%2Fdevices%2FDHT11&et=1729607740&method=md5&sign=VRwAjJjquF6I%2FyCFP%2FnnFQ%3D%3D" //MQTT连接ONENET的密码
//#define SubscribeTopic_Attribute "$sys/G7NqVv6le8/DHT11/thing/property/post/reply" //订阅的属性主题
//#define SubscribeTopic_Server "$sys/G7NqVv6le8/DHT11/thing/service/LED_CONTROL/invoke" //订阅的服务主题
//#define PublishTopic   "$sys/G7NqVv6le8/DHT11/thing/property/post" //发布的主题
//#define MQTTPUBLISH(temp, humi) "{\"id\":\"123\",\"params\":{\"temp\":{\"value\":%.2lf},\"humi\":{\"value\":%.2lf}}}",temp, humi //发送onejson数据
/************************	阿里云服务器***************************************/
#define MQTT_Client_ID  "6a83c960cbb0cf6bb97a572f_tdd123_0_0_2026081803" 
#define MQTT_User_Name  "6a83c960cbb0cf6bb97a572f_tdd123" //设备ID
#define MQTT_Password  "b3269520b75e5df17e02424f81c40e1f19c5bcb5f52a6d5ac689b36bb434206a" //MQTT连接ONENET的密码
#define SubscribeTopic_Server_Reply "$oc/devices/6a83c960cbb0cf6bb97a572f_tdd123/sys/commands/request_id={request_id}" //订阅的属性主题
#define SubscribeTopic_Server "$oc/devices/6a83c960cbb0cf6bb97a572f_tdd123/sys/messages/down"
//#define SubscribeTopic_Server_web "/k2877qwTkuz/shebei1/user/get" 
#define PublishTopic   "$oc/devices/6a83c960cbb0cf6bb97a572f_tdd123/sys/properties/report" //发布的主题
//#define PublishTopic_web   "/k2877qwTkuz/shebei32/user/update" //发布的主题

//去掉LED2，新增gas；v2 协议额外带 fan 和 alarm 告警位掩码
#define MQTTPUBLISH(temp, humi, gas, fan, alarm)"{\"services\":[{\"service_id\":\"show\",\"properties\":{\"humi\":%f,\"temp\":%f,\"gas\":%f,\"fan\":%d,\"alarm\":%d},\"event_time\":\"20151212T121212Z\"}]}",humi,temp,gas,fan,alarm

void MQTT_Clear(void);
void MQTT_SendData(uint8_t* buf,uint16_t len);
void MQTT_SendHeart(void);
void MQTT_Init(void);
void MQTT_Connect(void);
void MQTT_Disconnect(void);
void MQTT_PublicTopic(float humi,float temp, float gas, uint8_t fan, uint8_t alarm);
//void MQTT_PublicTopic_web(float temp,float humi, bool LED2);
void MQTT_SubscribeTopic(void);
void MQTT_SubscribeTopic_web(void);
void MQTT_UNSubscribeTopic(void);

#endif
