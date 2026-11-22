#ifndef DSPET_DEEPSEEK_H
#define DSPET_DEEPSEEK_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

enum { kDSPetDeepSeekKeyBytes = 128 };

/**
 * @brief 消费一段 24 kHz 单声道 PCM16LE，块边界可能跨样本。
 * @param[in] bytes 仅在回调期间有效的数据。
 * @param[in] count 字节数。
 * @param[in,out] context 调用方的回调状态。
 * @return ESP_OK 表示接受，其余错误终止下载。
 */
typedef esp_err_t (*DSPetDeepSeekAudioSink)(const uint8_t *bytes, size_t count, void *context);

/**
 * @brief 用项目的角色提示对一个文本问题生成回复。
 * @param[in] key 调用期间借用的 DeepSeek 凭据。
 * @param[in] question UTF-8 文本问题。
 * @param[out] reply 成功时写入以 NUL 结束的 UTF-8 回复。
 * @param[in] capacityBytes 输出缓冲字节数，包含终止符。
 * @return ESP_OK 表示获得完整回复，其余为上游、格式或传输错误。
 */
esp_err_t DSPetDeepSeekReply(const char *key, const char *question, char *reply, size_t capacityBytes);

/**
 * @brief 合成 Stella 语音并逐块交给 PCM 消费者。
 * @param[in] key 调用期间借用的 DeepSeek 凭据。
 * @param[in] text UTF-8 朗读正文。
 * @param[in] sink 接收音频的同步回调。
 * @param[in,out] context 透传给回调的状态。
 * @return ESP_OK 表示读取到完整音频，其余为上游、传输或回调错误。
 */
esp_err_t DSPetDeepSeekSpeak(const char *key, const char *text, DSPetDeepSeekAudioSink sink, void *context);

#endif
