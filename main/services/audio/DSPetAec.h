#ifndef DSPET_AEC_H
#define DSPET_AEC_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** @brief 一路全双工回声消除会话。 */
typedef struct DSPetAec DSPetAec;

/**
 * @brief 创建 16 kHz、单麦克风和单播放参考的回声消除实例。
 * @param[out] aec 成功时写入实例，失败时输出 NULL。
 * @return ESP_OK 表示成功，其余为参数或资源错误。
 * @note 同一任务使用实例，按连续时序处理音频。
 */
esp_err_t DSPetAecCreate(DSPetAec **aec);

/**
 * @brief 查询一帧需要的样本数。
 * @param[in] aec 已创建实例。
 * @return 每路每帧的 PCM 样本数。
 */
size_t DSPetAecFrameSamples(const DSPetAec *aec);

/**
 * @brief 处理已按时间对齐的一帧麦克风和播放参考。
 * @param[in,out] aec 已创建的实例。
 * @param[in] mic 麦克风单声道 PCM16，sampleCount 个样本。
 * @param[in] reference 连续的播放参考 PCM16，sampleCount 个样本。
 * @param[out] output 处理结果，容量至少为 sampleCount 个样本。
 * @param[in] sampleCount 必须与 DSPetAecFrameSamples 返回值一致。
 * @return ESP_OK 表示输出一帧，其余表示参数错误。
 * @note 三个缓冲仅在调用期间借用。输出有算法帧延迟。
 */
esp_err_t
DSPetAecProcess(DSPetAec *aec, const int16_t *mic, const int16_t *reference, int16_t *output, size_t sampleCount);

/**
 * @brief 释放回声消除实例和内部工作缓冲。
 * @param[in] aec 待释放实例，可为 NULL。
 */
void DSPetAecDestroy(DSPetAec *aec);

#endif
