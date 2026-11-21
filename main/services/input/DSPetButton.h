#ifndef DSPET_BUTTON_H
#define DSPET_BUTTON_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/**
 * @brief 初始化按键并启动采样任务。
 * @return ESP_OK 表示成功，否则为 GPIO 驱动、资源分配或重复初始化错误。
 */
esp_err_t DSPetButtonStart(void);

/**
 * @brief 非阻塞读取最新的有效按下累计次数，允许累计次数回绕。
 * @param[out] count 成功时写入累计次数。
 * @return 有新值时返回 true，否则保留原值并返回 false。
 * @note 启动后由单个应用任务消费。快速连续按下可以合并通知。
 */
bool DSPetButtonTakeCount(uint32_t *count);

#endif
