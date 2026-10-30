/**
 * @file bsp_hx711.h
 * @brief HX711 称重模块驱动。
 * @details 对应的 bsp_hx711.c 通过 GPIO 时序读取 HX711 A 通道的 24 位有符号
 *          ADC 原始值。DT 接 PB0，SCK 接 PB1，模块与 STM32 共地并使用 3.3V 供电。
 *          当前由 SensorTask 测试原始数值；后续加入去皮、标定后换算试剂剩余重量。
 */
#ifndef BSP_HX711_H
#define BSP_HX711_H

#include <stdint.h>

/* 本次空载和 558.5 g 标定结果；后续应保存到 W25Q64 参数区。 */
#define HX711_DEFAULT_TARE_RAW           445165L
#define HX711_DEFAULT_COUNTS_PER_GRAM    493.35f

/** 将 SCK 置低并初始化微秒计时器。 */
void HX711_Init(void);

/** DT 为低电平表示一组 ADC 数据已经准备完成。 */
uint8_t HX711_IsReady(void);

/**
 * @brief 读取 A 通道、增益 128 的一组 24 位有符号原始计数。
 * @param rawValue 输出原始计数。
 * @return 1 表示成功，0 表示 DT 等待超时。
 */
uint8_t HX711_ReadRaw(int32_t *rawValue);

/**
 * @brief 连续读取多组数据后取算术平均值，降低单次采样抖动。
 * @param rawValue 输出平均后的原始计数。
 * @param sampleCount 采样次数，建议 3~10。
 * @return 1 表示成功，0 表示参数错误或任意一次读取超时。
 */
uint8_t HX711_ReadAverage(int32_t *rawValue, uint8_t sampleCount);

/** 更新去皮值和标定系数；countsPerGram 必须大于 0。 */
void HX711_SetCalibration(int32_t tareRaw, float countsPerGram);

/** 读取多次平均后的重量，单位为 g。 */
uint8_t HX711_ReadWeightGram(float *weightGram, uint8_t sampleCount);

/**
 * @brief 等待称台稳定并返回抗干扰后的重量。
 * @details 连续采集 9 次，排序后使用中间 5 次求平均；中间数据波动过大或
 *          重量超出系统允许范围时返回失败，防止异常数据进入追溯记录。
 * @param weightGram 输出稳定重量，单位为 g。
 * @return 1 表示重量有效且稳定，0 表示读取失败、波动过大或数值越界。
 */
uint8_t HX711_ReadStableWeightGram(float *weightGram);

#endif /* BSP_HX711_H */
