/**
 * @file bsp_w25qxx.h
 * @brief W25QXX 外部 Flash 驱动和离线缓存模块。
 * @details 对应的 bsp_w25qxx.c 使用 STM32 硬件 SPI2 实现 JEDEC ID 读取、
 *          写使能、忙状态等待、4 KB 扇区擦除、页写入和数据读取。
 *          后续用于网络断开时缓存采集记录、参数和运行日志，不用于当前程序运行。
 *          SPI2：PB13-SCK、PB14-MISO、PB15-MOSI；CS 接 PB12。
 *          W25Q64 只能使用 3.3 V 供电，不能连接 5 V。
 *          后续由 StorageTask 使用；访问前需要获取 SpiMutex。
 
 *    W25Q64 后续主要存储：
 *   MQTT 断网期间的传感器和操作记录
 *    恢复联网后等待补发的数据
 *    设备参数
 *    故障及运行日志
 *    离线缓存管理应等所有传感器和 MQTT 流程跑通后再接入
 */
#ifndef BSP_W25QXX_H
#define BSP_W25QXX_H

#include <stdint.h>

/** 释放片选并等待芯片稳定。 */
void W25QXX_Init(void);

/** 读取 JEDEC ID，例如 W25Q64 通常返回 EF 40 17。 */
uint8_t W25QXX_ReadJedecId(uint8_t *manufacturer,
                           uint8_t *memoryType,
                           uint8_t *capacity);

/** 读取任意长度的数据。 */
uint8_t W25QXX_Read(uint32_t address, uint8_t *data, uint16_t length);

/** 写入一页内的数据，不能超过 256 字节或跨越页边界。 */
uint8_t W25QXX_PageProgram(uint32_t address,
                           const uint8_t *data,
                           uint16_t length);

/** 擦除 address 所在的 4 KB 扇区。 */
uint8_t W25QXX_EraseSector(uint32_t address);

#endif /* BSP_W25QXX_H */
