#ifndef DSPET_BOOT_PIN_H
#define DSPET_BOOT_PIN_H

#include <stdbool.h>

#include "esp_err.h"

/**
 * @brief 配置板载 BOOT 引脚为上拉输入。
 * @return ESP_OK 表示成功，其余返回 GPIO 驱动错误。
 */
esp_err_t DSPetBootPinInit(void);

/**
 * @brief 读取 BOOT 键物理状态。
 * @return 引脚为低电平时返回 true。
 * @note 初始化后在任务上下文调用。
 */
bool DSPetBootPinIsPressed(void);

#endif
