#ifndef DSPET_PCM_24K_TO_16K_H
#define DSPET_PCM_24K_TO_16K_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief 临时保存未填满转换窗口的 PCM 样本，初始置零。 */
typedef struct {
    int16_t samples[3];  /**< 一组输入样本。 */
    size_t  used;        /**< 本组有效的样本数。 */
    bool    haveLowByte; /**< 当前输入流带有跨块的不完整 PCM16 样本。 */
    uint8_t lowByte;     /**< 尚未配对的低字节。 */
} DSPetPcm24kTo16k;

/**
 * @brief 使用相邻样本的加权平均，每三个 24 kHz PCM16 样本生成两个 16 kHz 样本。
 * @param[in,out] state 当前输出流的独立状态。
 * @param[in] sample 一个带符号的 24 kHz 输入样本。
 * @param[out] output 容纳两个样本。仅有输出时写入。
 * @return 本次生成的样本数，为 0 或 2。
 */
size_t DSPetPcm24kTo16kPush(DSPetPcm24kTo16k *state, int16_t sample, int16_t output[2]);

/**
 * @brief 将一块可能跨样本边界的 24 kHz PCM16LE 转换为 16 kHz PCM。
 * @param[in,out] state 同一语音流复用的转换状态。
 * @param[in] bytes 小端输入字节。
 * @param[in] byteCount 输入字节数。
 * @param[out] output 容量至少为 byteCount / 2 + 2 个样本。
 * @return 实际写入的样本数。
 */
size_t DSPetPcm24kTo16kDecode(DSPetPcm24kTo16k *state, const uint8_t *bytes, size_t byteCount, int16_t *output);

#endif
