#ifndef DSPET_ANIMATION_H
#define DSPET_ANIMATION_H

#include <stddef.h>
#include <stdint.h>
#include "DSPetClip.h"
#include "esp_err.h"

/** @brief 当前帧以及下一帧的调度时间。 */
typedef struct {
    const uint8_t *pixels; /**< 借用的 RGB565_BE 像素，到下次取帧前有效。 */
    size_t sizeBytes; /**< 像素数据的字节数。 */
    unsigned index; /**< 片段内的帧号，从零开始。 */
    int64_t nextFrameUs; /**< 下一帧的绝对时刻，与输入使用同一时钟。 */
} DSPetAnimationFrame;

/**
 * @brief 初始化动画服务的解码缓冲。
 * @param[in] pixelCount 素材每帧的像素数。
 * @return ESP_OK 表示成功，否则返回参数、状态或内存分配错误。
 * @note 初始化和取帧由同一任务调用。
 */
esp_err_t DSPetAnimationInit(size_t pixelCount);

/**
 * @brief 按时间取得当前动画帧，必要时从关键帧恢复。
 * @param[in] clip 经构建系统验证的常驻片段。
 * @param[in] startedUs 当前轮播放开始时刻，单位微秒。
 * @param[in] nowUs 当前时刻，单位微秒。
 * @param[out] frame 成功时写入帧视图。
 * @return ESP_OK 表示成功，否则为参数、状态或素材解码错误。
 */
esp_err_t DSPetAnimationFrameAt(const DSPetClip *clip, int64_t startedUs, int64_t nowUs,
                               DSPetAnimationFrame *frame);

#endif
