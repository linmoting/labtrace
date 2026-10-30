#include "bsp_w25qxx.h"
#include "main.h"
#include "spi.h"
#include "app_tasks.h"

#define W25QXX_CMD_WRITE_ENABLE  0x06U
#define W25QXX_CMD_READ_STATUS_1 0x05U
#define W25QXX_CMD_PAGE_PROGRAM  0x02U
#define W25QXX_CMD_SECTOR_ERASE  0x20U
#define W25QXX_CMD_READ_DATA     0x03U
#define W25QXX_CMD_JEDEC_ID      0x9FU
#define W25QXX_STATUS_BUSY       0x01U
#define W25QXX_DUMMY_BYTE        0xFFU
#define W25QXX_PAGE_SIZE         256U
#define W25QXX_SPI_TIMEOUT_MS    100U

static void W25QXX_Select(void)
{
    HAL_GPIO_WritePin(W25Q_CS_GPIO_Port, W25Q_CS_Pin, GPIO_PIN_RESET);
}

static void W25QXX_Deselect(void)
{
    HAL_GPIO_WritePin(W25Q_CS_GPIO_Port, W25Q_CS_Pin, GPIO_PIN_SET);
}

static uint8_t W25QXX_Transfer(uint8_t transmit, uint8_t *receive)
{
    return (HAL_SPI_TransmitReceive(&hspi2, &transmit, receive, 1U,
                                    W25QXX_SPI_TIMEOUT_MS) == HAL_OK) ? 1U : 0U;
}

static uint8_t W25QXX_SendAddress(uint32_t address)
{
    uint8_t command[3];

    command[0] = (uint8_t)(address >> 16U);
    command[1] = (uint8_t)(address >> 8U);
    command[2] = (uint8_t)address;
    return (HAL_SPI_Transmit(&hspi2, command, sizeof(command),W25QXX_SPI_TIMEOUT_MS) == HAL_OK) ? 1U : 0U;
}

static uint8_t W25QXX_WriteEnable(void)
{
    uint8_t command = W25QXX_CMD_WRITE_ENABLE;

    W25QXX_Select();
    if (HAL_SPI_Transmit(&hspi2, &command, 1U, W25QXX_SPI_TIMEOUT_MS) != HAL_OK)
    {
        W25QXX_Deselect();
        return 0U;
    }
    W25QXX_Deselect();
    return 1U;
}

static uint8_t W25QXX_WaitWhileBusy(uint32_t timeoutMs)
{
    uint8_t command = W25QXX_CMD_READ_STATUS_1;
    uint8_t status;
    uint32_t start = HAL_GetTick();

    W25QXX_Select();
    if (HAL_SPI_Transmit(&hspi2, &command, 1U, W25QXX_SPI_TIMEOUT_MS) != HAL_OK)
    {
        W25QXX_Deselect();
        return 0U;
    }

    do
    {
        if (W25QXX_Transfer(W25QXX_DUMMY_BYTE, &status) == 0U)
        {
            W25QXX_Deselect();
            return 0U;
        }
        if ((status & W25QXX_STATUS_BUSY) == 0U)
        {
            W25QXX_Deselect();
            return 1U;
        }
    } while ((HAL_GetTick() - start) < timeoutMs);

    W25QXX_Deselect();
    return 0U;
}

void W25QXX_Init(void)
{
    W25QXX_Deselect();
    HAL_Delay(10U);
}

uint8_t W25QXX_ReadJedecId(uint8_t *manufacturer,
                           uint8_t *memoryType,
                           uint8_t *capacity)
{
    uint8_t command = W25QXX_CMD_JEDEC_ID;
    uint8_t values[3];

    if ((manufacturer == NULL) || (memoryType == NULL) || (capacity == NULL))
    {
        return 0U;
    }

    W25QXX_Select();
    if ((HAL_SPI_Transmit(&hspi2, &command, 1U, W25QXX_SPI_TIMEOUT_MS) != HAL_OK) ||
        (HAL_SPI_Receive(&hspi2, values, sizeof(values), W25QXX_SPI_TIMEOUT_MS) != HAL_OK))
    {
        W25QXX_Deselect();
        return 0U;
    }
    W25QXX_Deselect();

    *manufacturer = values[0];
    *memoryType = values[1];
    *capacity = values[2];
    return 1U;
}

uint8_t W25QXX_Read(uint32_t address, uint8_t *data, uint16_t length)
{
    uint8_t command = W25QXX_CMD_READ_DATA;

    if ((data == NULL) || (length == 0U))
    {
        return 0U;
    }

    W25QXX_Select();
    if ((HAL_SPI_Transmit(&hspi2, &command, 1U, W25QXX_SPI_TIMEOUT_MS) != HAL_OK) ||
        (W25QXX_SendAddress(address) == 0U) ||
        (HAL_SPI_Receive(&hspi2, data, length, W25QXX_SPI_TIMEOUT_MS) != HAL_OK))
    {
        W25QXX_Deselect();
        return 0U;
    }
    W25QXX_Deselect();
    return 1U;
}

uint8_t W25QXX_PageProgram(uint32_t address,const uint8_t *data,uint16_t length)
{
    uint8_t command = W25QXX_CMD_PAGE_PROGRAM;

    if ((data == NULL) || (length == 0U) || (length > W25QXX_PAGE_SIZE) ||
        (((address & (W25QXX_PAGE_SIZE - 1U)) + length) > W25QXX_PAGE_SIZE))
    {
        return 0U;
    }
    if ((W25QXX_WaitWhileBusy(1000U) == 0U) ||
        (W25QXX_WriteEnable() == 0U))
    {
        return 0U;
    }

    W25QXX_Select();
    if ((HAL_SPI_Transmit(&hspi2, &command, 1U, W25QXX_SPI_TIMEOUT_MS) != HAL_OK) ||
        (W25QXX_SendAddress(address) == 0U) ||
        (HAL_SPI_Transmit(&hspi2, (uint8_t *)data, length,
                          W25QXX_SPI_TIMEOUT_MS) != HAL_OK))
    {
        W25QXX_Deselect();
        return 0U;
    }
    W25QXX_Deselect();
    return W25QXX_WaitWhileBusy(1000U);
}

uint8_t W25QXX_EraseSector(uint32_t address)
{
    uint8_t command = W25QXX_CMD_SECTOR_ERASE;

    if ((W25QXX_WaitWhileBusy(1000U) == 0U) ||(W25QXX_WriteEnable() == 0U))
    {
        return 0U;
    }

    W25QXX_Select();
    if ((HAL_SPI_Transmit(&hspi2, &command, 1U, W25QXX_SPI_TIMEOUT_MS) != HAL_OK) ||
        (W25QXX_SendAddress(address & 0xFFF000UL) == 0U))
    {
        W25QXX_Deselect();
        return 0U;
    }
    W25QXX_Deselect();
    return W25QXX_WaitWhileBusy(3000U);
}

