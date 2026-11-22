#ifndef DSPET_AEC_TEST_H
#define DSPET_AEC_TEST_H

#include "esp_err.h"

/**
 * @brief 播放本地中文，在实机回录中比较 AEC 前后的功率。
 * @return ESP_OK 表示测量窗口有效且通过阈值，其余为测试或驱动错误。
 * @note 在独占 I²S 的测试任务中执行。测试时请保持安静。
 */
esp_err_t DSPetAecTestRun(void);

#endif
