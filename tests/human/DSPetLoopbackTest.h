#ifndef DSPET_LOOPBACK_TEST_H
#define DSPET_LOOPBACK_TEST_H

#include "esp_err.h"

/**
 * @brief 提示用户在提示音后说话，并执行录音回放。
 * @return ESP_OK 表示板上采集和回放通过，可进入听感确认。否则返回底层错误。
 */
esp_err_t DSPetLoopbackTestRun(void);

#endif
