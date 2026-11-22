#ifndef DSPET_DUPLEX_TEST_H
#define DSPET_DUPLEX_TEST_H

#include "esp_err.h"

/**
 * @brief 验证 PSRAM、双路 DMA、取消、自然播完及参考回静音。
 * @return ESP_OK 表示全部通过，其余为驱动或测试错误。
 * @note 在专用测试任务运行。调用前启动播放服务，释放音频设备。
 */
esp_err_t DSPetDuplexTestRun(void);

#endif
