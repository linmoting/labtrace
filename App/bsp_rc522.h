/**
 * @file bsp_rc522.h
 * @brief RC522 RFID 读卡器驱动模块。
 * @details 对应的 bsp_rc522.c 通过 SPI1 配置 MFRC522、开启天线、寻卡并读取
 *          常见 MIFARE 卡的 4 字节 UID，用于识别试剂卡和操作员工牌卡。
 *          SPI1：PA5-SCK、PA6-MISO、PA7-MOSI；片选 SDA/SS 接 PA4，RST 接 PA11。
 *          IRQ/RQ 暂不接，先由 RfidTask 周期轮询；RC522 必须使用 3.3V 供电。
 */
#ifndef BSP_RC522_H
#define BSP_RC522_H

#include <stdint.h>

/* 已登记的 4 字节 UID：后续按 uid[0]~uid[3] 逐字节进行身份匹配。 */
#define RC522_OPERATOR_UID_0 0x49U
#define RC522_OPERATOR_UID_1 0x02U
#define RC522_OPERATOR_UID_2 0x95U
#define RC522_OPERATOR_UID_3 0x05U

#define RC522_REAGENT_UID_0  0xD7U
#define RC522_REAGENT_UID_1  0x35U
#define RC522_REAGENT_UID_2  0xF4U
#define RC522_REAGENT_UID_3  0x05U

typedef enum
{
    RC522_STATUS_OK = 0U,
    RC522_STATUS_NO_CARD,
    RC522_STATUS_ERROR
} RC522_Status_t;

typedef enum
{
    RC522_CARD_UNKNOWN = 0U,
    RC522_CARD_OPERATOR,
    RC522_CARD_REAGENT
} RC522_CardType_t;

/** 硬件复位并配置 RC522；成功返回 1。 */
uint8_t RC522_Init(void);

/**
 * @brief 检测并分类一张常见 4 字节 UID 卡。
 * @param uid 输出读取到的 UID，可传 NULL。
 * @param cardType 输出卡类型，成功时必填。
 * @return OK、NO_CARD 或 ERROR。
 */
RC522_Status_t RC522_ReadCard(uint8_t uid[4], RC522_CardType_t *cardType);

#endif /* BSP_RC522_H */
