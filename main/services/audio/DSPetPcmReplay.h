#ifndef DSPET_PCM_REPLAY_H
#define DSPET_PCM_REPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief 调整本地录音回放音量时的计量结果。 */
typedef struct {
    int32_t dc;      /**< 整段录音的直流分量。 */
    int32_t peak;    /**< 去直流后的峰值。 */
    double  rms;     /**< 去直流后的原始 RMS。 */
    double  metered; /**< 限制尖峰后的 RMS。 */
    double  gain;    /**< 应用的增益。 */
} DSPetPcmReplayLevel;

/**
 * @brief 就地调整回放音量，限制增益和输出峰值。
 * @param[in,out] pcm 有效 PCM16 缓冲。
 * @param[in] sampleCount 样本数。
 * @param[out] level 计量结果。
 * @return 存在可回放内容时返回 true，静音或参数无效返回 false。
 */
bool DSPetPcmReplayNormalize(int16_t *pcm, size_t sampleCount, DSPetPcmReplayLevel *level);

#endif
