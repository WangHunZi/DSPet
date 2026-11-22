#ifndef DSPET_PCM_WAV_H
#define DSPET_PCM_WAV_H

#include <stdint.h>

enum { kDSPetPcmWavHeaderBytes = 44 };

/**
 * @brief 生成 16 位单声道 PCM 的 WAV 文件头，后接小端样本。
 * @param[out] header 容纳 44 字节。
 * @param[in] sampleRateHz 采样率。
 * @param[in] sampleCount 样本数。两倍样本数加 36 须在 uint32_t 范围内。
 */
void DSPetPcmWavHeader(uint8_t header[kDSPetPcmWavHeaderBytes], uint32_t sampleRateHz, uint32_t sampleCount);

#endif
