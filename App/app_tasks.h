/**
 * @file app_tasks.h
 * @brief 应用层任务模块。
 * @details app_tasks.c 存放传感器采集、RFID 读卡、追溯记录、离线存储和健康监测的任务循环。
 *          当前只保留任务骨架，后续逐个模块测试成功后将业务代码填入对应任务。
 *          由 freertos.c 的 CubeMX 任务入口调用。
 */
#ifndef APP_TASKS_H
#define APP_TASKS_H

#include "app_types.h"

/* 第一版本地演练使用的固定时间；DS3231 到货后替换为真实读取。 */
#define APP_FIXED_TIMESTAMP "2026-09-15 12:00:00"

void AppTask_RunSensor(void *argument);
void AppTask_RunRfid(void *argument);
void AppTask_RunTrace(void *argument);
void AppTask_RunStorage(void *argument);
void AppTask_RunHealth(void *argument);

/** MQTT 发布成功后通知 StorageTask 更新 Flash 槽位状态。 */
void StorageTask_HandleCommand(const StorageCommand_t *command);

/** DS3231 到货后的单次测试：通过 USART1 输出设备应答和当前时间。 */
void AppTask_TestDs3231Once(void);

#endif /* APP_TASKS_H */
