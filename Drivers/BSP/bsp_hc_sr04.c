#include "bsp_hc_sr04.h"
#include "main.h"

/* STM32F103 的系统时钟由 CubeMX 配置为 72 MHz。 */
#define HCSR04_CPU_MHZ       72U
#define HCSR04_TIMEOUT_US    30000U
#define HCSR04_MIN_DISTANCE_CM 2U
#define HCSR04_MAX_DISTANCE_CM 400U
#define HCSR04_SAMPLE_COUNT   3U

static void HCSR04_DelayUs(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * HCSR04_CPU_MHZ;

    while ((DWT->CYCCNT - start) < cycles)
    {
        /* 忙等待仅用于极短的 Trig 脉冲和 Echo 计时。 */
    }
}

static uint32_t HCSR04_ElapsedUs(uint32_t start, uint32_t end)
{
    return (end - start) / HCSR04_CPU_MHZ;
}

void HCSR04_Init(void)
{
    /* 开启 DWT 周期计数器，提供比 HAL_GetTick() 更精确的微秒计时。 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    HAL_GPIO_WritePin(HCSR04_TRIG_GPIO_Port, HCSR04_TRIG_Pin, GPIO_PIN_RESET);
    /* 让模块上电后留出稳定时间。 */
    HAL_Delay(100U);
}

uint8_t HCSR04_ReadDistanceCm(uint32_t *distanceCm)
{
    uint32_t waitStart;
    uint32_t echoStart;
    uint32_t echoEnd;
    uint32_t timeoutCycles = HCSR04_TIMEOUT_US * HCSR04_CPU_MHZ;

    if (distanceCm == NULL)
    {
        return 0U;
    }

    /* 发送 HC-SR04 要求的至少 10 us 高电平触发脉冲。 */
    HAL_GPIO_WritePin(HCSR04_TRIG_GPIO_Port, HCSR04_TRIG_Pin, GPIO_PIN_RESET);
    HCSR04_DelayUs(2U);
    HAL_GPIO_WritePin(HCSR04_TRIG_GPIO_Port, HCSR04_TRIG_Pin, GPIO_PIN_SET);
    HCSR04_DelayUs(10U);
    HAL_GPIO_WritePin(HCSR04_TRIG_GPIO_Port, HCSR04_TRIG_Pin, GPIO_PIN_RESET);

    /* 等待 Echo 上升沿，超时必须退出，避免任务永久阻塞。 */
    waitStart = DWT->CYCCNT;
    while (HAL_GPIO_ReadPin(HCSR04_ECHO_GPIO_Port, HCSR04_ECHO_Pin) == GPIO_PIN_RESET)
    {
        if ((DWT->CYCCNT - waitStart) > timeoutCycles)
        {
            return 0U;
        }
    }
    echoStart = DWT->CYCCNT;

    /* 等待 Echo 下降沿并计算高电平持续时间。 */
    while (HAL_GPIO_ReadPin(HCSR04_ECHO_GPIO_Port, HCSR04_ECHO_Pin) == GPIO_PIN_SET)
    {
        if ((DWT->CYCCNT - echoStart) > timeoutCycles)
        {
            return 0U;
        }
    }
    echoEnd = DWT->CYCCNT;

    /* 声速换算：距离(cm)约等于 Echo 脉宽(us) / 58。 */
    *distanceCm = HCSR04_ElapsedUs(echoStart, echoEnd) / 58U;

    /* 忽略超出 HC-SR04 典型工作范围的结果。 */
    if ((*distanceCm < HCSR04_MIN_DISTANCE_CM) ||
        (*distanceCm > HCSR04_MAX_DISTANCE_CM))
    {
        return 0U;
    }

    return 1U;
}

uint8_t HCSR04_ReadMedianDistanceCm(uint32_t *distanceCm)
{
    uint32_t samples[HCSR04_SAMPLE_COUNT];
    uint32_t value;
    uint8_t count = 0U;
    uint8_t i;
    uint8_t j;

    if (distanceCm == NULL)
    {
        return 0U;
    }

    /* 采集 3 次有效值，并按从小到大排序后取中间值。 */
    for (i = 0U; i < HCSR04_SAMPLE_COUNT; ++i)
    {
        if (HCSR04_ReadDistanceCm(&value) != 0U)
        {
            samples[count++] = value;
        }

        /* 避免上一次回波影响下一次测量。 */
        HCSR04_DelayUs(60000U);
    }

    if (count == 0U)
    {
        return 0U;
    }

    for (i = 0U; i < count; ++i)
    {
        for (j = (uint8_t)(i + 1U); j < count; ++j)
        {
            if (samples[j] < samples[i])
            {
                value = samples[i];
                samples[i] = samples[j];
                samples[j] = value;
            }
        }
    }

    *distanceCm = samples[count / 2U];
    return 1U;
}
