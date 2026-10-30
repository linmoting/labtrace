/**
 * @file bsp_hc_sr04.h
 * @brief HC-SR04 超声波测距模块的接口。
 * @details 对应实现文件 bsp_hc_sr04.c 负责给 Trig 输出 10 us 触发脉冲，
 *          测量 Echo 高电平时间并换算距离。Trig 接 STM32 PB9，Echo 接 PB8；
 *          HC-SR04 的 Echo 通常是 5 V，必须先经过分压再接 PB8。
 *          当前由 FreeRTOS 的 SensorTask 周期调用，用于测试距离读取。
 */
#ifndef BSP_HC_SR04_H
#define BSP_HC_SR04_H

#include <stdint.h>

/** 初始化超声波触发脚，并启动 Cortex-M3 的 DWT 微秒计时器。 */
void HCSR04_Init(void);

/**
 * @brief 读取一次距离。
 * @param distanceCm 输出距离，单位为厘米。
 * @return 1 表示读取成功，0 表示 Echo 超时或传感器未响应。
 */
uint8_t HCSR04_ReadDistanceCm(uint32_t *distanceCm);

/**
 * @brief 连续采样并取中值，降低超声波反射造成的跳变。
 * @param distanceCm 输出滤波后的距离，单位为厘米。
 * @return 1 表示至少有一次有效采样，0 表示全部采样超时或越界。
 */
uint8_t HCSR04_ReadMedianDistanceCm(uint32_t *distanceCm);

#endif /* BSP_HC_SR04_H */
