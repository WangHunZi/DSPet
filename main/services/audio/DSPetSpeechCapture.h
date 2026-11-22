#ifndef DSPET_SPEECH_CAPTURE_H
#define DSPET_SPEECH_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    kDSPetSpeechFrameSamples   = 320,
    kDSPetSpeechPreRollFrames  = 16,
    kDSPetSpeechPreRollSamples = kDSPetSpeechFrameSamples * kDSPetSpeechPreRollFrames,
};

/** @brief 一帧处理后对应用可见的变化。 */
typedef enum {
    kDSPetSpeechNoEvent,  /**< 状态未变化。 */
    kDSPetSpeechStarted,  /**< 已确认开口，录音包含预录。 */
    kDSPetSpeechFinished, /**< 已到句尾或录音容量已满。 */
} DSPetSpeechEvent;

/** @brief 短预录和单句 PCM 录音状态，由同一处理任务持有。 */
typedef struct {
    int16_t  history[kDSPetSpeechPreRollSamples]; /**< 最近 320 ms PCM。 */
    int16_t *recording;                           /**< 调用方持有的录音缓冲。 */
    size_t   capacitySamples;                     /**< 录音缓冲容量。 */
    size_t   recordedSamples;                     /**< 已写入的录音样本数。 */
    size_t   preRollSamples;                      /**< 开口事件时保存的样本数。 */
    size_t   index;                               /**< 下次写入历史的位置。 */
    size_t   filled;                              /**< 历史中有效的样本数。 */
    unsigned voicedFrames;                        /**< 连续命中人声的帧数。 */
    unsigned quietFrames;                         /**< 录音中连续未命中人声的帧数。 */
    bool     active;                              /**< 已经开始录音。 */
    bool     done;                                /**< 已经结束本句。 */
} DSPetSpeechCapture;

/**
 * @brief 初始化短预录和单句录音。
 * @param[out] capture 非空状态。
 * @param[in] recording PCM 录音缓冲，在整轮期间有效。
 * @param[in] capacitySamples 录音容量，至少能保存预录和一帧数据。
 */
void DSPetSpeechCaptureInit(DSPetSpeechCapture *capture, int16_t *recording, size_t capacitySamples);

/**
 * @brief 输入一帧 16 kHz、20 ms 的消回声 PCM。
 * @param[in,out] capture 已初始化状态。
 * @param[in] pcm kDSPetSpeechFrameSamples 个样本，仅在调用时借用。
 * @param[in] voiced 本帧是否有人声证据。
 * @param[in] armed 是否允许产生开口事件。关闭时仍保留短预录。
 * @return 应用可见的状态变化。
 */
DSPetSpeechEvent DSPetSpeechCapturePush(DSPetSpeechCapture *capture, const int16_t *pcm, bool voiced, bool armed);

#endif
