#ifndef DSPET_SETUP_PAGE_H
#define DSPET_SETUP_PAGE_H

#include "DSPetSetupPortal.h"
#include "esp_err.h"

/**
 * @brief 把热点入网二维码和备用网址刷到面板。
 * @param[in] hotspot 临时热点入网信息，调用期间有效。
 * @return ESP_OK 表示完成刷屏，其余为内存、编码或面板错误。
 * @note 由负责面板的任务调用。
 */
esp_err_t DSPetSetupPageDraw(const DSPetSetupHotspot *hotspot);

#endif
