#ifndef DSPET_AUDIO_CORRELATION_H
#define DSPET_AUDIO_CORRELATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief 麦克风与播放参考的相关峰。 */
typedef struct {
    size_t offset; /**< 最佳匹配起点，相对麦克风缓冲区的样本数。 */
    float  score;  /**< 去直流后的绝对归一化相关系数，范围 0 到 1。 */
} DSPetAudioCorrelation;

/**
 * @brief 在指定范围内找出参考信号与录音的最强匹配。
 * @param[in] reference 播放参考样本。
 * @param[in] referenceCount 播放参考样本数，至少为 2。
 * @param[in] mic 麦克风 PCM 样本。
 * @param[in] micCount 麦克风样本数。
 * @param[in] firstOffset 搜索首个起点，单位样本。
 * @param[in] lastOffset 搜索最后一个起点，包含该起点，单位样本。
 * @param[out] match 成功时写入相关峰。
 * @return 找到可计算的峰时返回 true。常量信号或非法范围返回 false。
 */
bool DSPetAudioCorrelationFind(const int16_t         *reference,
                               size_t                 referenceCount,
                               const int16_t         *mic,
                               size_t                 micCount,
                               size_t                 firstOffset,
                               size_t                 lastOffset,
                               DSPetAudioCorrelation *match);

#endif
