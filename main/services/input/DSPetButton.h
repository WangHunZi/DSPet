#ifndef DSPET_BUTTON_H
#define DSPET_BUTTON_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @brief 用 Espressif Button 组件初始化 BOOT。
 * @return ESP_OK 表示成功，否则为 GPIO 驱动、资源分配或重复初始化错误。
 */
esp_err_t DSPetButtonStart(void);

/**
 * @brief 非阻塞读取最新的有效短按累计次数，允许累计次数回绕。
 * @param[out] count 成功时写入累计次数。
 * @return 有新值时返回 true，否则保留原值并返回 false。
 * @note 启动后由单个应用任务消费。快速连续按下可以合并通知。
 */
bool DSPetButtonTakeCount(uint32_t *count);

/** @brief 消费一次达到长按时长的事件，挂起的多个长按合并为一次。 */
bool DSPetButtonTakeLongPress(void);

/** @brief 读取 BOOT 的物理电平，按住时返回 true。 */
bool DSPetButtonIsPressed(void);

/** @brief 供串口检查的按键事件累计数。 */
typedef struct {
    uint32_t shortPresses; /**< 小于一秒后松开的次数。 */
    uint32_t longPresses;  /**< 按住达到五秒的次数。 */
} DSPetButtonCounts;

/** @brief 读取累计计数，不消费按键事件。 */
DSPetButtonCounts DSPetButtonGetCounts(void);

#endif
