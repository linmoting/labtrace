#include "bsp_servo.h"
#include "tim.h"

#define SERVO_MIN_PULSE_US 1000U
#define SERVO_MAX_PULSE_US 2000U
#define SERVO_CENTER_US    1500U

/* 临时校准端点；装好盒门后按实际方向和限位调整这两个值。 */
#define SERVO_CLOSE_PULSE_US 1000U
#define SERVO_OPEN_PULSE_US  2000U

uint8_t Servo_Init(void)
{
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, SERVO_CENTER_US);
    return (HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1) == HAL_OK) ? 1U : 0U;
}

void Servo_SetPulseUs(uint16_t pulseUs)
{
    if (pulseUs < SERVO_MIN_PULSE_US)
    {
        pulseUs = SERVO_MIN_PULSE_US;
    }
    else if (pulseUs > SERVO_MAX_PULSE_US)
    {
        pulseUs = SERVO_MAX_PULSE_US;
    }

    /* TIM2 每计数一次为 1 us，CCR1 数值就是 PWM 高电平脉宽。 */
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, pulseUs);
}

void Servo_Open(void)
{
    Servo_SetPulseUs(SERVO_OPEN_PULSE_US);
}

void Servo_Close(void)
{
    Servo_SetPulseUs(SERVO_CLOSE_PULSE_US);
}
