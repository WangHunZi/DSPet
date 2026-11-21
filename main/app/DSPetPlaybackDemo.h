#ifndef DSPET_PLAYBACK_DEMO_H
#define DSPET_PLAYBACK_DEMO_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/**
 * @brief 初始化 BOOT 播放演示。
 * @return ESP_OK 表示演示就绪，否则返回素材或播放服务错误。
 */
esp_err_t DSPetPlaybackDemoInit(void);

/**
 * @brief 消费按键累计值与播放状态通知。
 * @param[in] presses 有效按键累计次数，允许回绕。
 * @note 空闲时按键开始播报，播报中按键请求停播。由应用循环调用。
 */
void DSPetPlaybackDemoUpdate(uint32_t presses);

/**
 * @brief 查询屏幕是否应显示播报动作。
 * @param[out] startedUs 正在播报时写入起始时刻，单位微秒。
 * @return 当前有已排入 DMA 且未排空的播报时返回 true。
 */
bool DSPetPlaybackDemoSpeaking(int64_t *startedUs);

#endif
