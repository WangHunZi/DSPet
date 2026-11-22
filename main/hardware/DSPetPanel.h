#ifndef DSPET_PANEL_H
#define DSPET_PANEL_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

enum {
    kDSPetPanelWidthPx    = 240,
    kDSPetPanelHeightPx   = 240,
    kDSPetPanelFrameBytes = kDSPetPanelWidthPx * kDSPetPanelHeightPx * 2,
};

/** @brief 初始化 ST7789、背光和发送缓冲，由同一个任务调用本模块。 */
void DSPetPanelInit(void);

/**
 * @brief 用一个 RGB565 颜色填充整屏并等待 DMA 完成。
 * @param[in] rgb565 像素值，写入屏幕时高字节在前。
 * @note 必须先初始化。发生硬件错误时终止运行。
 */
void DSPetPanelFill(uint16_t rgb565);

/**
 * @brief 发送整帧像素并等待 DMA 完成。
 * @param[in] pixels 行优先 RGB565 数据，高字节在前，调用期间有效。
 * @param[in] sizeBytes 输入缓冲区长度，必须等于 kDSPetPanelFrameBytes。
 * @return ESP_OK 表示 DMA 完成，其余返回参数或驱动错误。超时后停止接受新帧。
 */
esp_err_t DSPetPanelPresent(const uint8_t *pixels, size_t sizeBytes);

#endif
