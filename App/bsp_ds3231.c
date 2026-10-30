#include "bsp_ds3231.h"
#include "i2c.h"

#define DS3231_REG_TIME 0x00U

static uint8_t Ds3231_BcdToBin(uint8_t value)
{
    return (uint8_t)(((value >> 4U) * 10U) + (value & 0x0FU));
}

static uint8_t Ds3231_BinToBcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4U) | (value % 10U));
}

uint8_t DS3231_IsReady(void)
{
    return (HAL_I2C_IsDeviceReady(&hi2c1, DS3231_I2C_ADDRESS, 2U, 100U) == HAL_OK)
         ? 1U : 0U;
}

uint8_t DS3231_IsValidDateTime(const DS3231_DateTime_t *dateTime)
{
    if (dateTime == NULL || dateTime->seconds > 59U || dateTime->minutes > 59U ||
        dateTime->hours > 23U || dateTime->day < 1U || dateTime->day > 7U ||
        dateTime->date < 1U || dateTime->date > 31U || dateTime->month < 1U ||
        dateTime->month > 12U)
    {
        return 0U;
    }
    return 1U;
}

uint8_t DS3231_ReadDateTime(DS3231_DateTime_t *dateTime)
{
    uint8_t data[7];

    if (dateTime == NULL || HAL_I2C_Mem_Read(&hi2c1, DS3231_I2C_ADDRESS,
        DS3231_REG_TIME, I2C_MEMADD_SIZE_8BIT, data, sizeof(data), 200U) != HAL_OK)
    {
        return 0U;
    }

    dateTime->seconds = Ds3231_BcdToBin(data[0] & 0x7FU);
    dateTime->minutes = Ds3231_BcdToBin(data[1] & 0x7FU);
    dateTime->hours = Ds3231_BcdToBin(data[2] & 0x3FU); /* 24 小时制 */
    dateTime->day = Ds3231_BcdToBin(data[3] & 0x07U);
    dateTime->date = Ds3231_BcdToBin(data[4] & 0x3FU);
    dateTime->month = Ds3231_BcdToBin(data[5] & 0x1FU);
    dateTime->year = Ds3231_BcdToBin(data[6]);

    return DS3231_IsValidDateTime(dateTime);
}

uint8_t DS3231_SetDateTime(const DS3231_DateTime_t *dateTime)
{
    uint8_t data[7];

    if (DS3231_IsValidDateTime(dateTime) == 0U)
    {
        return 0U;
    }

    data[0] = Ds3231_BinToBcd(dateTime->seconds);
    data[1] = Ds3231_BinToBcd(dateTime->minutes);
    data[2] = Ds3231_BinToBcd(dateTime->hours);
    data[3] = Ds3231_BinToBcd(dateTime->day);
    data[4] = Ds3231_BinToBcd(dateTime->date);
    data[5] = Ds3231_BinToBcd(dateTime->month);
    data[6] = Ds3231_BinToBcd(dateTime->year);

    return (HAL_I2C_Mem_Write(&hi2c1, DS3231_I2C_ADDRESS, DS3231_REG_TIME,
        I2C_MEMADD_SIZE_8BIT, data, sizeof(data), 200U) == HAL_OK) ? 1U : 0U;
}
