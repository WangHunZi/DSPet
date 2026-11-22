#ifndef DSPET_SPEECH_DETECTOR_H
#define DSPET_SPEECH_DETECTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** @brief 一路 ESP-SR 人声判定。 */
typedef struct DSPetSpeechDetector DSPetSpeechDetector;

/**
 * @brief 初始化 16 kHz、20 ms 一帧的人声检测。
 * @param[out] detector 成功时返回实例，失败时写入 NULL。
 * @return ESP_OK 表示成功，其余为参数或资源错误。
 */
esp_err_t DSPetSpeechDetectorCreate(DSPetSpeechDetector **detector);

/**
 * @brief 从去回声的近端音频判断当前帧是否有人声。
 * @param[in,out] detector 同一任务持有的实例。
 * @param[in] pcm 320 个单声道 PCM16 样本，仅在调用时借用。
 * @param[out] rms 本帧限幅计量后的 PCM RMS。
 * @return 同时满足人声分类和能量门限时返回 true。
 */
bool DSPetSpeechDetectorProcess(DSPetSpeechDetector *detector, const int16_t *pcm, float *rms);

/**
 * @brief 释放人声检测实例。
 * @param[in] detector 待释放实例，可为 NULL。
 */
void DSPetSpeechDetectorDestroy(DSPetSpeechDetector *detector);

#endif
