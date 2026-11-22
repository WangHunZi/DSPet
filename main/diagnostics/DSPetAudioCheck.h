#ifndef DSPET_AUDIO_CHECK_H
#define DSPET_AUDIO_CHECK_H

#include "esp_err.h"

/**
 * @brief 播提示音，录音三秒并回放，然后释放音频设备。
 * @return ESP_OK 表示录到非恒定的 PCM 并完成回放，失败时返回错误。
 * @note 由单个任务独占音频设备。程序记录错误并返回。
 */
esp_err_t DSPetAudioCheckRun(void);

#endif
