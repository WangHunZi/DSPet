#ifndef DSPET_NEAR_SPEECH_H
#define DSPET_NEAR_SPEECH_H

#include "DSPetAudioDevice.h"
#include "DSPetSpeechCapture.h"
#include "esp_err.h"

/** @brief 单次聆听的回声消除、人声判定与录音服务。 */
typedef struct DSPetNearSpeech DSPetNearSpeech;

enum { kDSPetNearSpeechMaxRecordingSamples = 4 * kDSPetAudioDeviceSampleRateHz };

/** @brief 本帧的聆听条件。 */
typedef enum {
    kDSPetNearSpeechAlignOnly, /**< 保持 DMA 对齐，跳过消回声和人声判断。 */
    kDSPetNearSpeechDisarmed,  /**< 对齐和预热，不产生开口事件。 */
    kDSPetNearSpeechListening, /**< 喇叭静音，等待开口或录制。 */
    kDSPetNearSpeechPlaying,   /**< 喇叭正在播报，使用插话门限。 */
} DSPetNearSpeechMode;

/**
 * @brief 在 PSRAM 准备同步器、预录和单句缓冲。
 * @param[out] speech 成功时返回实例，失败时置空。
 * @return ESP_OK 表示成功，其余为资源或参数错误。
 */
esp_err_t DSPetNearSpeechCreate(DSPetNearSpeech **speech);

/**
 * @brief 开始新句，丢弃上一句的录音和预录，复用 AEC 与采样时间轴。
 * @param[in,out] speech 已创建的实例。
 * @param[in] restartTimeline 自上次调用以来未消费全部 DMA 块时设为 true。
 */
void DSPetNearSpeechReset(DSPetNearSpeech *speech, bool restartTimeline);

/**
 * @brief 在观测中断后重新对齐，清除跨断点的人声证据和预录。
 * @param[in,out] speech 已创建的实例，保留本句已经确认的录音。
 */
void DSPetNearSpeechRealign(DSPetNearSpeech *speech);

/**
 * @brief 消费一个麦克风或播放 DMA 块，按对齐后的 PCM 产生人声事件。
 * @param[in,out] speech 同一任务持有的实例。
 * @param[in] block DMA 观测副本，调用期间借用。
 * @param[in] mode 是否允许开口以及是否适用插话门限。
 * @param[out] event 本次输入产生的状态变化。
 * @return ESP_OK 表示接受，连续性丢失或处理失败时返回错误。
 */
esp_err_t DSPetNearSpeechPush(DSPetNearSpeech          *speech,
                              const DSPetAudioTapBlock *block,
                              DSPetNearSpeechMode       mode,
                              DSPetSpeechEvent         *event);

/**
 * @brief 读取已结束的单句录音。
 * @param[in] speech 已创建的实例。
 * @param[out] sampleCount 返回样本数。
 * @param[out] preRollSamples 返回保存的预录样本数。
 * @return 已结束时返回可借用的缓冲，否则返回 NULL。缓冲有效至开始新句或释放实例。
 */
int16_t *DSPetNearSpeechRecording(DSPetNearSpeech *speech, size_t *sampleCount, size_t *preRollSamples);

/**
 * @brief 查询武装期的峰值帧 RMS，供诊断门限。
 * @param[in] speech 已创建的实例。
 * @return 去回声后限幅计量的峰值帧 RMS。
 */
float DSPetNearSpeechPeakRms(const DSPetNearSpeech *speech);

/**
 * @brief 释放实例和录音。
 * @param[in] speech 释放目标，可为 NULL。
 */
void DSPetNearSpeechDestroy(DSPetNearSpeech *speech);

#endif
