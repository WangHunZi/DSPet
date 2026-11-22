#ifndef DSPET_DEEPSEEK_H
#define DSPET_DEEPSEEK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

enum { kDSPetDeepSeekKeyBytes = 128 };

/** @brief 由网络任务在传输边界查询的取消标记。 */
typedef struct {
    bool (*requested)(void *context); /**< 返回 true 时终止请求。 */
    void *context;                    /**< 调用期间有效的回调状态。 */
} DSPetDeepSeekCancel;

/** @brief 同一会话前一轮的问答。 */
typedef struct {
    const char *question;    /**< 用户上句话的原始转写。 */
    const char *reply;       /**< 上一轮回复正文。 */
    bool        interrupted; /**< 上一轮曾被打断或传输失败。 */
} DSPetDeepSeekHistory;

/**
 * @brief 消费一段 24 kHz 单声道 PCM16LE，块边界可能跨样本。
 * @param[in] bytes 仅在回调期间有效的数据。
 * @param[in] count 字节数。
 * @param[in,out] context 调用方的回调状态。
 * @return ESP_OK 表示接受，其余错误终止下载。
 */
typedef esp_err_t (*DSPetDeepSeekAudioSink)(const uint8_t *bytes, size_t count, void *context);

/**
 * @brief 将单声道 PCM16 录音封装为 WAV 交给 DeepSeek ASR。
 * @param[in] key 调用期间借用的 DeepSeek 凭据。
 * @param[in] samples 非空录音缓冲，调用返回前有效。
 * @param[in] sampleCount 录音样本数。
 * @param[in] sampleRateHz 录音采样率，为 8000 或 16000。
 * @param[out] transcript 以 NUL 结尾的 UTF-8 转写。
 * @param[in] capacityBytes 转写缓冲字节数。
 * @param[in] cancel 可空的取消标记。
 * @return ESP_OK 表示有转写，ESP_ERR_NOT_FOUND 表示未识别到内容，其余为网络或取消错误。
 */
esp_err_t DSPetDeepSeekTranscribe(const char                *key,
                                  const int16_t             *samples,
                                  size_t                     sampleCount,
                                  uint32_t                   sampleRateHz,
                                  char                      *transcript,
                                  size_t                     capacityBytes,
                                  const DSPetDeepSeekCancel *cancel);

/**
 * @brief 用项目角色提示生成完整的朗读正文，可先判断转写是否在对桌宠说话。
 * @param[in] key 调用期间借用的 DeepSeek 凭据。
 * @param[in] question UTF-8 文本问题。
 * @param[in] history 可空的上一轮问答。
 * @param[in] addressedOnly 为 true 时，模型可忽略没有对桌宠说的话。
 * @param[out] reply 成功时写入以 NUL 结束的 UTF-8 回复。
 * @param[in] capacityBytes 输出缓冲字节数，包含终止符。
 * @param[in] cancel 可空的取消标记。
 * @return ESP_OK 表示获得完整回复，ESP_ERR_NOT_FOUND 表示模型选择忽略，其余为上游、格式、传输或取消错误。
 */
esp_err_t DSPetDeepSeekReply(const char                 *key,
                             const char                 *question,
                             const DSPetDeepSeekHistory *history,
                             bool                        addressedOnly,
                             char                       *reply,
                             size_t                      capacityBytes,
                             const DSPetDeepSeekCancel  *cancel);

/**
 * @brief 合成 Stella 语音并逐块交给 PCM 消费者。
 * @param[in] key 调用期间借用的 DeepSeek 凭据。
 * @param[in] text UTF-8 朗读正文。
 * @param[in] sink 接收音频的同步回调。
 * @param[in,out] context 透传给回调的状态。
 * @param[in] cancel 可空的取消标记。
 * @return ESP_OK 表示读取到完整音频，其余为上游、传输或回调错误。
 */
esp_err_t DSPetDeepSeekSpeak(
    const char *key, const char *text, DSPetDeepSeekAudioSink sink, void *context, const DSPetDeepSeekCancel *cancel);

#endif
