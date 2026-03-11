#include "AIR780E.h"
#include "Delay.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>   // 若printf依赖stdio，确保已包含

#define RX_BUFFER_SIZE 256
#define PRINTF_BUFFER_SIZE 256   // 可根据需要调整

static uint8_t rxBuffer[RX_BUFFER_SIZE];
static volatile uint16_t rxHead = 0, rxTail = 0;
static volatile uint8_t rxNewData = 0;  // 可选标志，用于快速判断

static uint8_t g_network_initialized = 0;

/* 外部声明printf使用的串口（通常是USART1），此处仅用于接收回调打印 */
extern int printf(const char *format, ...);

/* 初始化USART2，波特率可配置，默认8位数据、1停止位、无校验 */
void AIR780E_UART_Init(uint32_t baudrate)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    /* 使能USART2和GPIOA时钟 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* 配置PA2为复用推挽输出（USART2_TX） */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 配置PA3为浮空输入（USART2_RX） */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* USART2基本配置 */
    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART2, &USART_InitStructure);

    /* 配置接收中断 */
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);

    /* 配置NVIC（USART2中断优先级，可根据需要调整） */
    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 使能USART2 */
    USART_Cmd(USART2, ENABLE);
}

/* 发送单字节（阻塞方式） */
void AIR780E_SendByte(uint8_t data)
{
    while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
    USART_SendData(USART2, data);
}

/* 发送字符串（阻塞方式） */
void AIR780E_SendString(char *str)
{
    while (*str) 
	{
        AIR780E_SendByte(*str++);
    }
}

/* 发送指定长度数据（阻塞方式） */
void AIR780E_SendData(uint8_t *data, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++) 
	{
        AIR780E_SendByte(data[i]);
    }
}

static void ClearRxBuffer(void)
{
    rxHead = 0;
    rxTail = 0;
}

/* 等待模块返回特定字符串，如"OK"或"CONNECT"，超时时间ms */
static uint8_t WaitResponse(char *expected, uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    while (elapsed < timeout_ms) {
        // 将环形缓冲区中的数据线性化到临时数组
        char temp[RX_BUFFER_SIZE + 1];
        uint16_t idx = rxTail;
        uint16_t len = 0;
        while (idx != rxHead && len < RX_BUFFER_SIZE) {
            temp[len++] = rxBuffer[idx];
            idx = (idx + 1) % RX_BUFFER_SIZE;
        }
        temp[len] = '\0';

        // 搜索期望字符串
        if (strstr(temp, expected) != NULL) {
            return 0;  // 成功
        }

        Delay_ms(1);   // 等待1ms再试
        elapsed++;
    }
    return 1;  // 超时
}

static uint8_t SendATCmdAndWait(char *cmd, char *expected, uint32_t timeout)
{
    ClearRxBuffer();
    AIR780E_SendString(cmd);
    return WaitResponse(expected, timeout);
}

uint8_t AIR780E_InitNetwork(void)
{
    uint8_t retry;

    // 重试 AT 指令，确保通信建立
    retry = 5;
    while (retry--) {
        if (SendATCmdAndWait("AT\r\n", "OK", 2000) == 0) {
            break;
        }
        Delay_ms(1000);
    }
    if (retry == 0) return 1;   // 通信失败

    // 重试 SIM 卡检测
    retry = 5;
    while (retry--) {
        if (SendATCmdAndWait("AT+CPIN?\r\n", "READY", 5000) == 0) {
            break;  // SIM 卡就绪
        }
        Delay_ms(2000);  // 等待更长时间再试
    }
    if (retry == 0) return 2;   // SIM 卡未就绪

    // 查询信号质量（可选）
    AIR780E_SendString("AT+CSQ\r\n");
    WaitResponse("OK", 3000);

    // 等待网络注册（已有重试机制）
    retry = 10;
    while (retry--) {
        ClearRxBuffer();
        AIR780E_SendString("AT+CREG?\r\n");
        if (WaitResponse("+CREG: 0,1", 2000) == 0 || WaitResponse("+CREG: 0,5", 2000) == 0) {
            break;
        }
        Delay_ms(1000);
    }
    if (retry == 0) return 3;

    // 附着 GPRS
    if (SendATCmdAndWait("AT+CGATT=1\r\n", "OK", 10000)) return 4;

    // 设置 PDP 上下文
    if (SendATCmdAndWait("AT+CGDCONT=1,\"IP\",\"CMNET\"\r\n", "OK", 5000)) return 5;

    // 激活 PDP 上下文
    if (SendATCmdAndWait("AT+CGACT=1,1\r\n", "OK", 30000)) return 6;

    // 查询 IP（可选）
    ClearRxBuffer();
    AIR780E_SendString("AT+CGPADDR=1\r\n");
    WaitResponse("OK", 1000);

    return 0;
}

uint8_t AIR780E_ConnectToServer(char *server, uint16_t port)
{
    char cmd[64];
    sprintf(cmd, "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", server, port);
    
    ClearRxBuffer();
    AIR780E_SendString(cmd);
    
    /* 等待 CONNECT OK 或 ALREADY CONNECT */
    if (WaitResponse("CONNECT OK", 15000) == 0) {
        return 0;  // 连接成功
    }
    return 1;  // 连接失败
}

uint8_t AIR780E_SendDataViaTCP(uint8_t *data, uint16_t len)
{
    char cmd[20];
    sprintf(cmd, "AT+CIPSEND=%d\r\n", len);
    
    ClearRxBuffer();
    AIR780E_SendString(cmd);
    
    /* 等待 '>' 提示符 */
    if (WaitResponse(">", 5000)) return 1;
    
    /* 发送实际数据（无需回车换行） */
    AIR780E_SendData(data, len);
    
    /* 等待 SEND OK */
    if (WaitResponse("SEND OK", 10000)) return 2;
    
    return 0;
}

void AIR780E_CloseTCP(void)
{
    AIR780E_SendString("AT+CIPCLOSE\r\n");
    WaitResponse("CLOSE OK", 5000);
}

/**
 * @brief 发送数据，如果连接断开则自动重连
 * @param server 服务器IP或域名
 * @param port 端口号
 * @param data 要发送的数据指针
 * @param len 数据长度
 * @param max_retry 最大重试次数（包括首次尝试）
 * @return 0成功，非0失败
 */
uint8_t AIR780E_SendWithReconnect(char *server, uint16_t port, uint8_t *data, uint16_t len, uint8_t max_retry)
{
    uint8_t retry = max_retry;
    while (retry--) {
        // 尝试发送数据
        uint8_t result = AIR780E_SendDataViaTCP(data, len);
        if (result == 0) {
            return 0;   // 发送成功
        }

        // 发送失败，可能连接已断开，尝试重连
        printf("Send failed, reconnecting...\r\n");
        AIR780E_CloseTCP();  // 确保之前连接关闭
        Delay_ms(1000);

        if (AIR780E_ConnectToServer(server, port) != 0) {
            printf("Reconnect failed, retry later...\r\n");
            Delay_ms(3000);
            continue;   // 重连失败，继续尝试
        }
        printf("Reconnected, retry send...\r\n");
    }
    return 1;   // 所有重试均失败
}

uint8_t AIR780E_EstablishLongConnection(char *server, uint16_t port)
{
    // 1. 初始化网络（如果尚未初始化）
    if (!g_network_initialized) {
        if (AIR780E_InitNetwork() != 0) {
            return 1;   // 网络初始化失败
        }
        g_network_initialized = 1;
    }

    // 2. 尝试连接服务器（如果已连接，AT+CIPSTART会返回"ALREADY CONNECT"）
    //    我们的 WaitResponse 会等待 "CONNECT OK" 或 "ALREADY CONNECT" 吗？
    //    当前 WaitResponse 只等待 "CONNECT OK"，所以需要修改一下连接函数或在这里处理。
    //    简便方法：直接调用 AIR780E_ConnectToServer，如果返回失败，可以尝试重新连接。
    uint8_t retry = 3;
    while (retry--) {
        if (AIR780E_ConnectToServer(server, port) == 0) {
            return 0;   // 连接成功或已连接
        }
        // 连接失败，可能网络异常，稍后重试
        Delay_ms(2000);
    }
    return 1;   // 连接失败
}

int AIR780E_Printf(const char *format, ...)
{
    char buffer[PRINTF_BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (len < 0) {
        return -1;   // 格式化错误
    }
    if (len >= sizeof(buffer)) {
        // 输出被截断，可根据需要调整缓冲区大小
        len = sizeof(buffer) - 1;
    }

    // 通过TCP发送数据
    if (AIR780E_SendDataViaTCP((uint8_t*)buffer, len) == 0) {
        return len;   // 返回实际发送的字节数
    }
    return -1;        // 发送失败
}

/* USART2中断服务函数：收到数据后通过printf打印 */
void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
        uint8_t rxData = USART_ReceiveData(USART2);
        
        /* 调试打印（可保留） */
        printf("%c", rxData);
        
        /* 存入环形缓冲区（如果未满） */
        uint16_t nextHead = (rxHead + 1) % RX_BUFFER_SIZE;
        if (nextHead != rxTail) {  // 缓冲区未满
            rxBuffer[rxHead] = rxData;
            rxHead = nextHead;
        }
    }
}
