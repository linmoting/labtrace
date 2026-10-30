/**
 * @file bsp_servo.h
 * @brief SG90 舵机控制模块。
 * @details 对应的 bsp_servo.c 启动 TIM2_CH1 PWM，并通过设置脉宽控制 SG90 位置。
 *          信号线接 PA0；TIM2 配置为 50 Hz，计数每格 1 us，周期 20 ms。
 *          舵机 VCC 使用独立 5V 电源，GND 必须与 STM32 共地。
 *          当前由 SensorTask 测试开、关位置；具体脉宽在 bsp_servo.c 中校准。
 *    后续要做的优化：
 *		缓慢转动：脉宽逐步变化，避免纸门突然甩动。
 *    到位等待：开关指令后等待约 500~800 ms，再认为动作完成。
 *    断开 PWM：到位后停止 PWM，减少舵机抖动、发热和耗电；但门受力时可能失去保持力，因此要根据机械结构决定是否使用。
 *
 */
#ifndef BSP_SERVO_H
#define BSP_SERVO_H

#include <stdint.h>

/** PWM 从中位开始输出；返回 1 表示启动成功。 */
uint8_t Servo_Init(void);

/** 设置脉宽，单位 us；限制在 1000~2000 us 内以免过度转动。 */
void Servo_SetPulseUs(uint16_t pulseUs);

/** 发出开门位置指令；方向和位置需结合实际机械安装校准。 */
void Servo_Open(void);

/** 发出关门位置指令；舵机没有反馈，指令发出不代表门已到位。 */
void Servo_Close(void);

#endif /* BSP_SERVO_H */
