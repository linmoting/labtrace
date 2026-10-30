/**
 * @file bsp_dht11.h
 * @brief DHT11 温湿度传感器驱动模块。
 * @details 对应的 bsp_dht11.c 负责切换 PA1 输入/输出模式，实现 DHT11
 *          单总线启动时序、40 位数据读取、超时保护和校验和检查。
 *          DATA 接 PA1，VCC 接 3.3V/5V，GND 与 STM32 共地。
 *          当前由 SensorTask 每 2 秒读取并通过 USART1 测试；后续结果写入
 *          SensorDataQueue。若使用无上拉电阻的裸传感器，DATA 需外接上拉。
 */
#ifndef BSP_DHT11_H
#define BSP_DHT11_H

#include <stdint.h>

typedef struct
{
    uint8_t humidityInteger;
    uint8_t humidityDecimal;
    uint8_t temperatureInteger;
    uint8_t temperatureDecimal;
} DHT11_Data_t;

/** 初始化微秒计时和 DATA 引脚，并等待传感器上电稳定。 */
void DHT11_Init(void);

/** 读取并校验一组温湿度数据；成功返回 1，超时或校验失败返回 0。 */
uint8_t DHT11_Read(DHT11_Data_t *data);

#endif /* BSP_DHT11_H */
