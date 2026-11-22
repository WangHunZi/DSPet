#ifndef DSPET_SETUP_H
#define DSPET_SETUP_H

#include <stdbool.h>

#include "esp_err.h"

/**
 * @brief 按已保存配置启动直连，首次启动则进入配网。
 * @return ESP_OK 表示开始，或返回存储、网络或显示错误。
 * @note 在语音任务初始化后，由面板所属的应用任务调用。
 */
esp_err_t DSPetSetupStart(void);

/**
 * @brief 推进配网流程，并处理实体长按复位。
 * @param[in] reset 本轮收到长按后松开的事件时为 true。
 * @return 配网页拥有屏幕时返回 true。
 */
bool DSPetSetupUpdate(bool reset);

/** @brief 打印当前配网阶段和公开的临时热点名称。 */
void DSPetSetupPrintStatus(void);

/** @brief 按显式 USB 串口请求显示配网期临时热点口令。 */
void DSPetSetupPrintHotspot(void);

#endif
