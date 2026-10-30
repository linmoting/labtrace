/**
 * @file mqtt_at.h
 * @brief ESP8266 的 AT 指令和 MQTT 通信模块。
 * @details mqtt_at.c 完成 Wi-Fi 状态检查、MQTT 连接、数据发布、命令订阅和接收。
 *          使用 USART2：PA2 为 STM32_TX 接 ESP8266_RXD，PA3 为 STM32_RX 接 ESP8266_TXD。
 *          调试日志通过 USART1 的 PA9/PA10 输出到 CH340。
 *          由 ESP8266Task 调用。
 */
#ifndef MQTT_AT_H
#define MQTT_AT_H

/** 运行 ESP8266 任务：连接 MQTT、发布真实追溯记录并接收控制命令。 */
void MqttAt_RunTask(void *argument);

#endif /* MQTT_AT_H */
