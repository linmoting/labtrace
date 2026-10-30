#include "bsp_rc522.h"
#include "main.h"
#include "spi.h"

#define RC522_REG_COMMAND       0x01U
#define RC522_REG_COM_IRQ       0x04U
#define RC522_REG_ERROR         0x06U
#define RC522_REG_FIFO_DATA     0x09U
#define RC522_REG_FIFO_LEVEL    0x0AU
#define RC522_REG_CONTROL       0x0CU
#define RC522_REG_BIT_FRAMING   0x0DU
#define RC522_REG_MODE          0x11U
#define RC522_REG_TX_CONTROL    0x14U
#define RC522_REG_TX_ASK        0x15U
#define RC522_REG_T_MODE        0x2AU
#define RC522_REG_T_PRESCALER   0x2BU
#define RC522_REG_T_RELOAD_H    0x2CU
#define RC522_REG_T_RELOAD_L    0x2DU
#define RC522_REG_VERSION       0x37U

#define RC522_CMD_IDLE          0x00U
#define RC522_CMD_TRANSCEIVE    0x0CU
#define RC522_CMD_SOFT_RESET    0x0FU
#define RC522_PICC_WUPA         0x52U
#define RC522_PICC_ANTICOLL_CL1 0x93U

#define RC522_SPI_TIMEOUT_MS    100U
#define RC522_TRANSCEIVE_TIMEOUT_MS 30U

static void RC522_Select(void)
{
    HAL_GPIO_WritePin(RC522_CS_GPIO_Port, RC522_CS_Pin, GPIO_PIN_RESET);
}

static void RC522_Deselect(void)
{
    HAL_GPIO_WritePin(RC522_CS_GPIO_Port, RC522_CS_Pin, GPIO_PIN_SET);
}

static uint8_t RC522_WriteRegister(uint8_t reg, uint8_t value)
{
    uint8_t frame[2];

    frame[0] = (uint8_t)((reg << 1U) & 0x7EU);
    frame[1] = value;
    RC522_Select();
    if (HAL_SPI_Transmit(&hspi1, frame, sizeof(frame),
                         RC522_SPI_TIMEOUT_MS) != HAL_OK)
    {
        RC522_Deselect();
        return 0U;
    }
    RC522_Deselect();
    return 1U;
}

static uint8_t RC522_ReadRegister(uint8_t reg)
{
    uint8_t address = (uint8_t)(((reg << 1U) & 0x7EU) | 0x80U);
    uint8_t value = 0U;

    RC522_Select();
    HAL_SPI_Transmit(&hspi1, &address, 1U, RC522_SPI_TIMEOUT_MS);
    HAL_SPI_Receive(&hspi1, &value, 1U, RC522_SPI_TIMEOUT_MS);
    RC522_Deselect();
    return value;
}

static void RC522_SetBits(uint8_t reg, uint8_t mask)
{
    RC522_WriteRegister(reg, (uint8_t)(RC522_ReadRegister(reg) | mask));
}

static void RC522_ClearBits(uint8_t reg, uint8_t mask)
{
    RC522_WriteRegister(reg, (uint8_t)(RC522_ReadRegister(reg) & (uint8_t)(~mask)));
}

static uint8_t RC522_EnableAntenna(void)
{
    uint8_t attempt;
    uint8_t control;

    /* 0x80 是部分模块复位后的保留位状态，直接保留它并强制打开 bit0/bit1。 */
    for (attempt = 0U; attempt < 3U; ++attempt)
    {
        control = RC522_ReadRegister(RC522_REG_TX_CONTROL);
        RC522_WriteRegister(RC522_REG_TX_CONTROL, (uint8_t)(control | 0x03U));
        HAL_Delay(2U);

        if ((RC522_ReadRegister(RC522_REG_TX_CONTROL) & 0x03U) == 0x03U)
        {
            return 1U;
        }
    }
    return 0U;
}

static RC522_Status_t RC522_Transceive(const uint8_t *sendData,
                                       uint8_t sendLength,
                                       uint8_t *receiveData,
                                       uint8_t *receiveLength,
                                       uint16_t *receiveBits)
{
    uint8_t irq = 0U;
    uint8_t error;
    uint8_t fifoLength;
    uint8_t lastBits;
    uint8_t index;
    uint32_t start;
    uint16_t bits;

    RC522_WriteRegister(RC522_REG_COMMAND, RC522_CMD_IDLE);
    RC522_WriteRegister(RC522_REG_COM_IRQ, 0x7FU);
    RC522_SetBits(RC522_REG_FIFO_LEVEL, 0x80U);

    for (index = 0U; index < sendLength; ++index)
    {
        RC522_WriteRegister(RC522_REG_FIFO_DATA, sendData[index]);
    }

    RC522_WriteRegister(RC522_REG_COMMAND, RC522_CMD_TRANSCEIVE);
    RC522_SetBits(RC522_REG_BIT_FRAMING, 0x80U);

    start = HAL_GetTick();
    do
    {
        irq = RC522_ReadRegister(RC522_REG_COM_IRQ);
        if ((irq & 0x30U) != 0U)
        {
            break;
        }
    } while ((HAL_GetTick() - start) < RC522_TRANSCEIVE_TIMEOUT_MS);

    RC522_ClearBits(RC522_REG_BIT_FRAMING, 0x80U);

    error = RC522_ReadRegister(RC522_REG_ERROR);
    fifoLength = RC522_ReadRegister(RC522_REG_FIFO_LEVEL);
    lastBits = (uint8_t)(RC522_ReadRegister(RC522_REG_CONTROL) & 0x07U);

    bits = (fifoLength == 0U)
         ? 0U
         : (lastBits != 0U)
         ? (uint16_t)(((uint16_t)(fifoLength - 1U) * 8U) + lastBits)
         : (uint16_t)((uint16_t)fifoLength * 8U);

    if ((irq & 0x01U) != 0U)
    {
        return RC522_STATUS_NO_CARD;
    }
    if ((irq & 0x30U) == 0U)
    {
        return RC522_STATUS_NO_CARD;
    }
    if ((error & 0x1BU) != 0U)
    {
        return RC522_STATUS_ERROR;
    }

    if (receiveBits != NULL)
    {
        *receiveBits = bits;
    }

    if ((receiveData != NULL) && (receiveLength != NULL))
    {
        if (fifoLength > *receiveLength)
        {
            fifoLength = *receiveLength;
        }
        *receiveLength = fifoLength;
        for (index = 0U; index < fifoLength; ++index)
        {
            receiveData[index] = RC522_ReadRegister(RC522_REG_FIFO_DATA);
        }
    }
    return RC522_STATUS_OK;
}

uint8_t RC522_Init(void)
{
    uint8_t version;

    RC522_Deselect();
    HAL_GPIO_WritePin(RC522_RST_GPIO_Port, RC522_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(10U);
    HAL_GPIO_WritePin(RC522_RST_GPIO_Port, RC522_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(50U);

    RC522_WriteRegister(RC522_REG_COMMAND, RC522_CMD_SOFT_RESET);
    HAL_Delay(50U);
    RC522_WriteRegister(RC522_REG_T_MODE, 0x8DU);
    RC522_WriteRegister(RC522_REG_T_PRESCALER, 0x3EU);
    RC522_WriteRegister(RC522_REG_T_RELOAD_L, 30U);
    RC522_WriteRegister(RC522_REG_T_RELOAD_H, 0U);
    RC522_WriteRegister(RC522_REG_TX_ASK, 0x40U);
    RC522_WriteRegister(RC522_REG_MODE, 0x3DU);
    if (RC522_EnableAntenna() == 0U)
    {
        return 0U;
    }

    version = RC522_ReadRegister(RC522_REG_VERSION);
    return ((version == 0x91U) || (version == 0x92U) ||
            (version == 0x88U)) ? 1U : 0U;
}

static RC522_Status_t RC522_ReadUid(uint8_t uid[4])
{
    uint8_t request = RC522_PICC_WUPA;
    uint8_t response[5];
    uint8_t responseLength = 2U;
    uint16_t responseBits = 0U;
    uint8_t anticollision[2] = {RC522_PICC_ANTICOLL_CL1, 0x20U};
    uint8_t checksum;
    RC522_Status_t status;

    if (uid == NULL)
    {
        return RC522_STATUS_ERROR;
    }

    /* WUPA 只发送 7 位，用于发现新卡或重新唤醒已读取的卡。 */
    RC522_WriteRegister(RC522_REG_BIT_FRAMING, 0x07U);
    status = RC522_Transceive(&request, 1U, response,
                              &responseLength, &responseBits);
    if ((status != RC522_STATUS_OK) || (responseBits != 16U))
    {
        return (status == RC522_STATUS_ERROR) ? RC522_STATUS_ERROR
                                              : RC522_STATUS_NO_CARD;
    }

    RC522_WriteRegister(RC522_REG_BIT_FRAMING, 0x00U);
    responseLength = sizeof(response);
    responseBits = 0U;
    status = RC522_Transceive(anticollision, sizeof(anticollision), response,
                              &responseLength, &responseBits);
    if ((status != RC522_STATUS_OK) || (responseLength != 5U) ||
        (responseBits != 40U))
    {
        return RC522_STATUS_ERROR;
    }

    checksum = (uint8_t)(response[0] ^ response[1] ^ response[2] ^ response[3]);
    if (checksum != response[4])
    {
        return RC522_STATUS_ERROR;
    }

    uid[0] = response[0];
    uid[1] = response[1];
    uid[2] = response[2];
    uid[3] = response[3];
    return RC522_STATUS_OK;
}

static RC522_CardType_t RC522_ClassifyUid(const uint8_t uid[4])
{
    if ((uid[0] == RC522_OPERATOR_UID_0) &&
        (uid[1] == RC522_OPERATOR_UID_1) &&
        (uid[2] == RC522_OPERATOR_UID_2) &&
        (uid[3] == RC522_OPERATOR_UID_3))
    {
        return RC522_CARD_OPERATOR;
    }
    if ((uid[0] == RC522_REAGENT_UID_0) &&
        (uid[1] == RC522_REAGENT_UID_1) &&
        (uid[2] == RC522_REAGENT_UID_2) &&
        (uid[3] == RC522_REAGENT_UID_3))
    {
        return RC522_CARD_REAGENT;
    }
    return RC522_CARD_UNKNOWN;
}

RC522_Status_t RC522_ReadCard(uint8_t uid[4], RC522_CardType_t *cardType)
{
    uint8_t localUid[4];
    RC522_Status_t status;

    if (cardType == NULL)
    {
        return RC522_STATUS_ERROR;
    }

    status = RC522_ReadUid(localUid);
    if (status != RC522_STATUS_OK)
    {
        return status;
    }

    if (uid != NULL)
    {
        uid[0] = localUid[0];
        uid[1] = localUid[1];
        uid[2] = localUid[2];
        uid[3] = localUid[3];
    }
    *cardType = RC522_ClassifyUid(localUid);
    return RC522_STATUS_OK;
}
