#ifndef __AIR780E_H
#define __AIR780E_H

#include "stm32f10x.h"

/* 初始化USART2，用于与AIR780E通信 */
void AIR780E_UART_Init(uint32_t baudrate);

/* 发送单字节数据 */
void AIR780E_SendByte(uint8_t data);

/* 发送字符串（以'\0'结尾） */
void AIR780E_SendString(char *str);

/* 发送指定长度的数据 */
void AIR780E_SendData(uint8_t *data, uint16_t len);

/* 初始化网络：附着GPRS、激活场景、获取IP */
uint8_t AIR780E_InitNetwork(void);

/* 连接到TCP服务器，server可以是IP或域名，port为端口号 */
uint8_t AIR780E_ConnectToServer(char *server, uint16_t port);

/* 通过已建立的TCP连接发送数据，返回0成功，非0失败 */
uint8_t AIR780E_SendDataViaTCP(uint8_t *data, uint16_t len);

/* 关闭TCP连接 */
void AIR780E_CloseTCP(void);

uint8_t AIR780E_SendWithReconnect(char *server, uint16_t port, uint8_t *data, uint16_t len, uint8_t max_retry);

/* 建立长连接：初始化网络并连接到服务器，如果已经连接则直接返回成功 */
uint8_t AIR780E_EstablishLongConnection(char *server, uint16_t port);

/* 通过已建立的长连接发送格式化数据，类似于printf */
int AIR780E_Printf(const char *format, ...);

#endif /* __AIR780E_H */
