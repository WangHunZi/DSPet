#ifndef DSPET_PCM_24K_TO_16K_H
#define DSPET_PCM_24K_TO_16K_H

#include <stddef.h>
#include <stdint.h>

/** @brief 临时保存未填满转换窗口的 PCM 样本，初始置零。 */
typedef struct {
    int16_t samples[3]; /**< 一组输入样本。 */
    size_t  used;       /**< 本组有效的样本数。 */
} DSPetPcm24kTo16k;

/**
 * @brief 使用相邻样本的加权平均，每三个 24 kHz PCM16 样本生成两个 16 kHz 样本。
 * @param[in,out] state 当前输出流的独立状态。
 * @param[in] sample 一个带符号的 24 kHz 输入样本。
 * @param[out] output 容纳两个样本。仅有输出时写入。
 * @return 本次生成的样本数，为 0 或 2。
 */
size_t DSPetPcm24kTo16kPush(DSPetPcm24kTo16k *state, int16_t sample, int16_t output[2]);

#endif
