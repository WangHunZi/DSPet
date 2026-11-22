#ifndef DSPET_SETUP_TEST_H
#define DSPET_SETUP_TEST_H

#include "esp_err.h"

/**
 * @brief 在独立命名空间验证配置保存、读回和清除。
 * @return ESP_OK 表示通过，否则返回存储错误或 ESP_ERR_INVALID_RESPONSE。
 */
esp_err_t DSPetSetupTestStorage(void);

/** @brief 输出物理按键和官方手势回调的当前结果。 */
void DSPetSetupTestPrintButton(void);

#endif
