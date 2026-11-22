#ifndef DSPET_BARGE_IN_H
#define DSPET_BARGE_IN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** @brief 一轮本地双工播报的观察结果。 */
typedef struct {
    bool     interrupted;     /**< 播报实际因人声取消。 */
    bool     naturalEnd;      /**< 播报自然结束。 */
    size_t   recordedSamples; /**< 插话录音样本数。 */
    size_t   preRollSamples;  /**< 保留的句首预录样本数。 */
    int64_t  drainUs;         /**< 从应用发出取消到 DMA 排空的时间。 */
    unsigned lostBlocks;      /**< 连续性错误次数。 */
    float    peakRms;         /**< 武装期间近端帧 RMS 峰值。 */
} DSPetBargeInResult;

/**
 * @brief 播报并监听自然插话。收到开口事件后停播并录完一句。
 * @param[in] prompt 本地 16 kHz PCM16，整轮有效。
 * @param[in] sampleCount 播报 PCM 样本数。
 * @param[in] replay 是否回放去回声后的录音。
 * @param[out] result 本轮结果。
 * @return ESP_OK 表示本地流程完成，其余为输入、音频或超时错误。
 * @note 调用任务独占 I²S。播放服务须已初始化，等待释放音频期间绘制任务可继续运行。
 */
esp_err_t DSPetBargeInRun(const int16_t *prompt, size_t sampleCount, bool replay, DSPetBargeInResult *result);

#endif
