#ifndef DSPET_WAKE_WORD_H
#define DSPET_WAKE_WORD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** @brief ESP-SR 本地唤醒词检测器。 */
typedef struct DSPetWakeWord DSPetWakeWord;

/**
 * @brief 从 model 分区加载「你好小智」模型，并准备 16 kHz 单声道输入。
 * @param[out] wakeWord 成功时写入实例，失败时置空。
 * @return ESP_OK 表示可检测，其余为模型或分配错误。
 * @note 同一时间只使用一个实例，所有调用由同一任务发起。
 */
esp_err_t DSPetWakeWordCreate(DSPetWakeWord **wakeWord);

/**
 * @brief 将任意长度的麦克风 PCM 按模型帧边界输入。
 * @param[in,out] wakeWord 已创建的实例。
 * @param[in] samples 非空 16 kHz 单声道 PCM16，借用至返回。
 * @param[in] sampleCount PCM 样本数。
 * @return 本块识别到唤醒词为 true。
 */
bool DSPetWakeWordPush(DSPetWakeWord *wakeWord, const int16_t *samples, size_t sampleCount);

/**
 * @brief 清除会话前的唤醒历史和不足一帧的数据。
 * @param[in,out] wakeWord 已创建的实例。
 */
void DSPetWakeWordReset(DSPetWakeWord *wakeWord);

/**
 * @brief 释放模型句柄、输入缓冲和分区映射。
 * @param[in] wakeWord 待释放实例，可为 NULL。
 */
void DSPetWakeWordDestroy(DSPetWakeWord *wakeWord);

#endif
