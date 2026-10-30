#include "bsp_dht11.h"
#include "main.h"

#define DHT11_CPU_MHZ         72U
#define DHT11_EDGE_TIMEOUT_US 120U
#define DHT11_ONE_MIN_US       50U

static void DHT11_DelayUs(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * DHT11_CPU_MHZ;

    while ((DWT->CYCCNT - start) < cycles)
    {
    }
}

static void DHT11_SetOutput(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = DHT11_DATA_Pin;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT11_DATA_GPIO_Port, &gpio);
}

static void DHT11_SetInput(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = DHT11_DATA_Pin;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(DHT11_DATA_GPIO_Port, &gpio);
}

static uint8_t DHT11_WaitLevel(GPIO_PinState level, uint32_t timeoutUs)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t timeoutCycles = timeoutUs * DHT11_CPU_MHZ;

    while (HAL_GPIO_ReadPin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin) != level)
    {
        if ((DWT->CYCCNT - start) > timeoutCycles)
        {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t DHT11_ReadByte(uint8_t *value)
{
    uint8_t bit;
    uint32_t highStart;
    uint32_t highTimeUs;

    *value = 0U;
    for (bit = 0U; bit < 8U; ++bit)
    {
        if (DHT11_WaitLevel(GPIO_PIN_SET, DHT11_EDGE_TIMEOUT_US) == 0U)
        {
            return 0U;
        }

        highStart = DWT->CYCCNT;
        if (DHT11_WaitLevel(GPIO_PIN_RESET, DHT11_EDGE_TIMEOUT_US) == 0U)
        {
            return 0U;
        }
        highTimeUs = (DWT->CYCCNT - highStart) / DHT11_CPU_MHZ;

        *value <<= 1U;
        if (highTimeUs >= DHT11_ONE_MIN_US)
        {
            *value |= 1U;
        }
    }
    return 1U;
}

void DHT11_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    DHT11_SetInput();
    HAL_Delay(1000U);
}

uint8_t DHT11_Read(DHT11_Data_t *data)
{
    uint8_t raw[5];
    uint8_t index;
    uint8_t success = 0U;
    uint32_t primask;

    if (data == NULL)
    {
        return 0U;
    }

    /* 主机拉低至少 18 ms，通知 DHT11 开始发送数据。 */
    DHT11_SetOutput();
    HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin, GPIO_PIN_RESET);
    HAL_Delay(20U);

    /* 从释放总线到读取结束约 4 ms，避免中断破坏微秒级时序。 */
    primask = __get_PRIMASK();
    __disable_irq();
    HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin, GPIO_PIN_SET);
    DHT11_DelayUs(30U);
    DHT11_SetInput();

    if ((DHT11_WaitLevel(GPIO_PIN_RESET, DHT11_EDGE_TIMEOUT_US) != 0U) &&
        (DHT11_WaitLevel(GPIO_PIN_SET, DHT11_EDGE_TIMEOUT_US) != 0U) &&
        (DHT11_WaitLevel(GPIO_PIN_RESET, DHT11_EDGE_TIMEOUT_US) != 0U))
    {
        success = 1U;
        for (index = 0U; index < 5U; ++index)
        {
            if (DHT11_ReadByte(&raw[index]) == 0U)
            {
                success = 0U;
                break;
            }
        }
    }

    if (primask == 0U)
    {
        __enable_irq();
    }
    DHT11_SetInput();

    if ((success == 0U) ||
        ((uint8_t)(raw[0] + raw[1] + raw[2] + raw[3]) != raw[4]))
    {
        return 0U;
    }

    data->humidityInteger = raw[0];
    data->humidityDecimal = raw[1];
    data->temperatureInteger = raw[2];
    data->temperatureDecimal = raw[3];
    return 1U;
}
