/**
 * @file bsp_KeyLedBuzzer.h
 * @brief LED、蜂鸣器状态提示和预留按键模块。
 * @details 对应的 bsp_KeyLedBuzzer.c 使用 PA8 控制 LED、PA15 控制有源蜂鸣器，
 *          两者均为高电平有效。提示采用非阻塞状态机，由 HealthTask 周期调用；
 *          数据收发提示 1 次、服务端连接成功提示 2 次、异常提示 3 次。
 *          按键引脚暂未分配，后续需要时再增加。
 */
#ifndef BSP_KEY_LED_BUZZER_H
#define BSP_KEY_LED_BUZZER_H

#include <stdint.h>

typedef enum
{
    STATUS_INDICATOR_DATA = 1U,
    STATUS_INDICATOR_CONNECTED = 2U,
    STATUS_INDICATOR_ERROR = 3U
} StatusIndicatorEvent_t;

void StatusIndicator_Init(void);
void StatusIndicator_Notify(StatusIndicatorEvent_t event);
void StatusIndicator_Process(void);

void StatusLed_On(void);
void StatusLed_Off(void);
void Buzzer_On(void);
void Buzzer_Off(void);

#endif /* BSP_KEY_LED_BUZZER_H */
