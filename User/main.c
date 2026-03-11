#include "stm32f10x.h"
#include "Delay.h"
#include "Serial.h"
#include "AIR780E.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    /* 1. 配置NVIC优先级分组（必须在任何中断使能前设置） */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    /* 2. 初始化调试串口（用于printf打印） */
    Serial_Init();

    /* 3. 初始化AIR780E使用的USART2，并等待模块稳定 */
    AIR780E_UART_Init(9600);
    printf("Waiting for module to power up...\r\n");
    Delay_ms(5000);   // 模块上电需要足够时间启动，延长至5秒

    if (AIR780E_EstablishLongConnection("47.94.38.121", 8182) != 0) {
        printf("Failed to establish long connection.\r\n");
        while(1);
    }
    printf("Long connection established.\r\n");

    while(1)
    {
        // 模拟采集数据
        float temperature = 25.3;
        int humidity = 60;

        // 使用 AIR780E_Printf 发送格式化数据
        AIR780E_Printf("temp:%.1f, hum:%d\r\n", temperature, humidity);

        // 等待30秒
        Delay_ms(1000);
    }
}
