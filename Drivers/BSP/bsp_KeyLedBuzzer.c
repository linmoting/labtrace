#include "bsp_KeyLedBuzzer.h"
#include "main.h"

#define INDICATOR_ON_TIME_MS  100U
#define INDICATOR_OFF_TIME_MS 120U

static volatile uint8_t indicatorActive;
static volatile uint8_t indicatorOutputOn;
static volatile uint8_t indicatorPulsesLeft;
static volatile uint32_t indicatorNextChange;

void StatusLed_On(void)
{
    HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin, GPIO_PIN_SET);
}

void StatusLed_Off(void)
{
    HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin, GPIO_PIN_RESET);
}

void Buzzer_On(void)
{
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
}

void Buzzer_Off(void)
{
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
}

void StatusIndicator_Init(void)
{
    StatusLed_Off();
    Buzzer_Off();
    indicatorActive = 0U;
    indicatorOutputOn = 0U;
    indicatorPulsesLeft = 0U;
    indicatorNextChange = 0U;
}

void StatusIndicator_Notify(StatusIndicatorEvent_t event)
{
    uint8_t pulseCount = (uint8_t)event;
    uint32_t primask;

    if ((pulseCount == 0U) || (pulseCount > 3U))
    {
        return;
    }

    /* 新事件替换尚未完成的旧提示；短临界区防止 HealthTask 读到半更新状态。 */
    primask = __get_PRIMASK();
    __disable_irq();
    indicatorPulsesLeft = pulseCount;
    indicatorOutputOn = 1U;
    StatusLed_On();
    Buzzer_On();
    indicatorNextChange = HAL_GetTick() + INDICATOR_ON_TIME_MS;
    indicatorActive = 1U;
    if (primask == 0U)
    {
        __enable_irq();
    }
}

void StatusIndicator_Process(void)
{
    uint32_t now;

    if (indicatorActive == 0U)
    {
        return;
    }

    now = HAL_GetTick();
    if ((int32_t)(now - indicatorNextChange) < 0)
    {
        return;
    }

    if (indicatorOutputOn != 0U)
    {
        StatusLed_Off();
        Buzzer_Off();
        indicatorOutputOn = 0U;
        --indicatorPulsesLeft;

        if (indicatorPulsesLeft == 0U)
        {
            indicatorActive = 0U;
        }
        else
        {
            indicatorNextChange = now + INDICATOR_OFF_TIME_MS;
        }
    }
    else
    {
        StatusLed_On();
        Buzzer_On();
        indicatorOutputOn = 1U;
        indicatorNextChange = now + INDICATOR_ON_TIME_MS;
    }
}

