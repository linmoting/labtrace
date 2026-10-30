#include "bsp_hx711.h"
#include "main.h"

#define HX711_CPU_MHZ          72U
#define HX711_READY_TIMEOUT_MS 150U
#define HX711_STABLE_SAMPLE_COUNT 9U
#define HX711_STABLE_FIRST_INDEX  2U
#define HX711_STABLE_LAST_INDEX   6U
#define HX711_MAX_STABLE_SPREAD_G 3.0f
#define HX711_MIN_VALID_WEIGHT_G -5.0f
#define HX711_MAX_VALID_WEIGHT_G  2000.0f

static int32_t hx711TareRaw = HX711_DEFAULT_TARE_RAW;
static float hx711CountsPerGram = HX711_DEFAULT_COUNTS_PER_GRAM;

static void HX711_DelayUs(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * HX711_CPU_MHZ;

    while ((DWT->CYCCNT - start) < cycles)
    {
    }
}

void HX711_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* SCK 长时间为高会使 HX711 进入掉电模式，空闲时必须保持低。 */
    HAL_GPIO_WritePin(HX711_SCK_GPIO_Port, HX711_SCK_Pin, GPIO_PIN_RESET);
}

uint8_t HX711_IsReady(void)
{
    return (HAL_GPIO_ReadPin(HX711_DT_GPIO_Port, HX711_DT_Pin) == GPIO_PIN_RESET)
         ? 1U : 0U;
}

uint8_t HX711_ReadRaw(int32_t *rawValue)
{
    uint32_t raw = 0U;
    uint32_t start;
    uint32_t interruptState;
    uint8_t bit;

    if (rawValue == NULL)
    {
        return 0U;
    }

    /* 等待转换完成，DT 拉低；设置超时以避免断线后任务卡死。 */
    start = HAL_GetTick();
    while (HX711_IsReady() == 0U)
    {
        if ((HAL_GetTick() - start) >= HX711_READY_TIMEOUT_MS)
        {
            return 0U;
        }
    }

    /*
     * 24 个时钟读数据，第 25 个时钟选择下一次仍使用 A 通道、增益 128。
     * 临界区只覆盖约 100 us 的 GPIO 时序，防止任务切换让 SCK 高电平保持
     * 超过 60 us，导致 HX711 意外进入掉电状态并返回异常原始值。
     */
    interruptState = __get_PRIMASK();
    __disable_irq();
    for (bit = 0U; bit < 24U; ++bit)
    {
        HAL_GPIO_WritePin(HX711_SCK_GPIO_Port, HX711_SCK_Pin, GPIO_PIN_SET);
        HX711_DelayUs(2U);
        raw = (raw << 1U) |
              (uint32_t)HAL_GPIO_ReadPin(HX711_DT_GPIO_Port, HX711_DT_Pin);
        HAL_GPIO_WritePin(HX711_SCK_GPIO_Port, HX711_SCK_Pin, GPIO_PIN_RESET);
        HX711_DelayUs(2U);
    }

    HAL_GPIO_WritePin(HX711_SCK_GPIO_Port, HX711_SCK_Pin, GPIO_PIN_SET);
    HX711_DelayUs(2U);
    HAL_GPIO_WritePin(HX711_SCK_GPIO_Port, HX711_SCK_Pin, GPIO_PIN_RESET);
    HX711_DelayUs(2U);
    if (interruptState == 0U)
    {
        __enable_irq();
    }

    /* HX711 数据是 24 位补码，扩展为 int32_t 后便于后续去皮和标定。 */
    if ((raw & 0x00800000UL) != 0U)
    {
        raw |= 0xFF000000UL;
    }
    *rawValue = (int32_t)raw;
    return 1U;
}

uint8_t HX711_ReadAverage(int32_t *rawValue, uint8_t sampleCount)
{
    int64_t sum = 0;
    int32_t sample;
    uint8_t index;

    if ((rawValue == NULL) || (sampleCount == 0U) || (sampleCount > 16U))
    {
        return 0U;
    }

    for (index = 0U; index < sampleCount; ++index)
    {
        if (HX711_ReadRaw(&sample) == 0U)
        {
            return 0U;
        }
        sum += sample;
    }

    *rawValue = (int32_t)(sum / sampleCount);
    return 1U;
}

void HX711_SetCalibration(int32_t tareRaw, float countsPerGram)
{
    if (countsPerGram > 0.0f)
    {
        hx711TareRaw = tareRaw;
        hx711CountsPerGram = countsPerGram;
    }
}

uint8_t HX711_ReadWeightGram(float *weightGram, uint8_t sampleCount)
{
    int32_t rawValue;

    if ((weightGram == NULL) || (hx711CountsPerGram <= 0.0f) ||
        (HX711_ReadAverage(&rawValue, sampleCount) == 0U))
    {
        return 0U;
    }

    *weightGram = (float)(hx711TareRaw - rawValue) / hx711CountsPerGram;
    return 1U;
}

uint8_t HX711_ReadStableWeightGram(float *weightGram)
{
    float samples[HX711_STABLE_SAMPLE_COUNT];
    float value;
    float sum = 0.0f;
    uint8_t i;
    uint8_t j;

    if (weightGram == NULL)
    {
        return 0U;
    }

    /* 单次读取 9 组重量，随后排序以剔除偶发的高低尖峰。 */
    for (i = 0U; i < HX711_STABLE_SAMPLE_COUNT; ++i)
    {
        if (HX711_ReadWeightGram(&samples[i], 1U) == 0U)
        {
            return 0U;
        }
    }

    for (i = 0U; i < HX711_STABLE_SAMPLE_COUNT; ++i)
    {
        for (j = (uint8_t)(i + 1U); j < HX711_STABLE_SAMPLE_COUNT; ++j)
        {
            if (samples[j] < samples[i])
            {
                value = samples[i];
                samples[i] = samples[j];
                samples[j] = value;
            }
        }
    }

    /* 中间 5 个值仍相差过大，说明称台还在振动或采集受到干扰。 */
    if ((samples[HX711_STABLE_LAST_INDEX] -
         samples[HX711_STABLE_FIRST_INDEX]) > HX711_MAX_STABLE_SPREAD_G)
    {
        return 0U;
    }

    for (i = HX711_STABLE_FIRST_INDEX; i <= HX711_STABLE_LAST_INDEX; ++i)
    {
        sum += samples[i];
    }
    value = sum / (float)(HX711_STABLE_LAST_INDEX -
                          HX711_STABLE_FIRST_INDEX + 1U);

    /* 试剂称台当前按 0~2000 g 使用；越界值只能来自接线、供电或采样异常。 */
    if ((value < HX711_MIN_VALID_WEIGHT_G) ||
        (value > HX711_MAX_VALID_WEIGHT_G))
    {
        return 0U;
    }

    /* 空载附近允许少量负漂移，但对业务层统一显示为 0 g。 */
    *weightGram = (value < 0.0f) ? 0.0f : value;
    return 1U;
}
