/**
 * @file bsp_iwdg.h
 * @brief 独立看门狗应用层模块。
 * @details bsp_iwdg.c 封装 HAL_IWDG_Refresh()，由 HealthTask 在确认任务健康后刷新。
 *          IWDG 使用芯片内部 LSI 时钟，不占用任何 GPIO。
 *          不能在每个任务中随意刷新，否则无法发现任务卡死。
 */
#ifndef BSP_IWDG_H
#define BSP_IWDG_H

#endif /* BSP_IWDG_H */
