#ifndef DSPET_BARGE_IN_QUIET_TEST_H
#define DSPET_BARGE_IN_QUIET_TEST_H

#include "esp_err.h"

/**
 * @brief 播放固定播报并验证安静环境下能够自然播完。
 * @return 自然结束且未误停时返回 ESP_OK，其余为测试或驱动错误。
 */
esp_err_t DSPetBargeInQuietTestRun(void);

#endif
