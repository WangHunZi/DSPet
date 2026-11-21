#ifndef DSPET_INDEXED_FRAME_H
#define DSPET_INDEXED_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { kDSPetIndexedFramePaletteBytes = 512 };

/**
 * @brief 恢复本帧的索引，并更新上一帧索引缓冲。
 * @param[in,out] payload 调色板及 pixelCount 个索引或异或差分字节。
 * @param[in,out] previous 独立缓冲区，至少有 pixelCount 字节。
 * @param[in] pixelCount 索引数。
 * @param[in] keyframe 关键帧传 true，直接采用完整索引。
 */
void DSPetIndexedFrameRestoreDelta(uint8_t *payload, uint8_t *previous, size_t pixelCount, bool keyframe);

/**
 * @brief 将调色板索引原地展开为高字节在前的 RGB565 像素。
 * @param[in,out] buffer 调色板及像素索引，返回后存放 RGB565。
 * @param[in] pixelCount 待展开的像素数。
 * @param[in] capacityBytes 缓冲区容量，单位字节。
 * @return 成功返回 true，参数无效时返回 false。
 */
bool DSPetIndexedFrameExpand(uint8_t *buffer, size_t pixelCount, size_t capacityBytes);

#endif
