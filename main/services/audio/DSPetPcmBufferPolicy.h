#ifndef DSPET_PCM_BUFFER_POLICY_H
#define DSPET_PCM_BUFFER_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 决定设备侧 16 kHz、PCM16 单声道何时可以开始播放。
 * @param[in] bytes 已缓冲的 PCM 字节数。
 * @param[in] capacityBytes 缓冲总容量。
 * @param[in] elapsedMs 从收到第一个 PCM 块算起的时间。尚未收到时传 0。
 * @param[in] complete 是否收到完整并通过校验的回复。
 * @return true 表示可开始播放。慢于消费速度时等待完整回复，容量接近用尽时也允许播放。
 */
bool DSPetPcmBufferCanStart(size_t bytes, size_t capacityBytes, uint32_t elapsedMs, bool complete);

/**
 * @brief 断粮后等待回复完整或缓冲快满，再恢复播放，减少反复切断一句话。
 * @param[in] bytes 已缓冲的 PCM 字节数。
 * @param[in] capacityBytes 缓冲总容量。
 * @param[in] complete 是否收到完整并通过校验的回复。
 * @return true 表示可以结束等待。
 */
bool DSPetPcmBufferCanResume(size_t bytes, size_t capacityBytes, bool complete);

#endif
