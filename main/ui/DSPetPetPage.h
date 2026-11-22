#ifndef DSPET_PET_PAGE_H
#define DSPET_PET_PAGE_H

#include <stdint.h>

#include "DSPetClip.h"
#include "esp_err.h"

/**
 * @brief 绘制桌宠页当前帧并提交屏幕。
 * @param[in] clip 由应用选定的动作。
 * @param[in] startedUs 动作开始时间，单位微秒。
 * @param[out] nextFrameUs 成功时写入下一帧的绝对时间，单位微秒。
 * @return ESP_OK 表示该帧发送完成，其余返回解码或面板错误。
 * @note 与动画服务和面板共用同一个调用任务。
 */
esp_err_t DSPetPetPageDraw(const DSPetClip *clip, int64_t startedUs, int64_t *nextFrameUs);

#endif
