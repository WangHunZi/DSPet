#ifndef DSPET_BARGE_IN_TEST_H
#define DSPET_BARGE_IN_TEST_H

#include "esp_err.h"

/**
 * @brief 验证用户插话可停播，回放录音供人听辨整句。
 * @return 停播与回放成功时返回 ESP_OK，否则返回测试或驱动错误。
 */
esp_err_t DSPetBargeInSpeechTestRun(void);

/**
 * @brief 播报期间由用户敲键盘，验证不会误停。
 * @return 自然结束且未误停时返回 ESP_OK，其余为测试或驱动错误。
 */
esp_err_t DSPetBargeInKeyboardTestRun(void);

#endif
