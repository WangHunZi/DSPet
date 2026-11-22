#ifndef DSPET_ALIGNMENT_TEST_H
#define DSPET_ALIGNMENT_TEST_H

#include "esp_err.h"

/**
 * @brief 播放短扫频并估计三轮 TX 参考到麦克风的延迟。
 * @return ESP_OK 表示三轮相关峰足够明确且延迟稳定，其余为测试或驱动错误。
 * @note 与双路自动测试共用播放服务，需在独占 I²S 的测试任务中运行。
 */
esp_err_t DSPetAlignmentTestRun(void);

#endif
