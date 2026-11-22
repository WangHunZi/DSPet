#ifndef DSPET_DUPLEX_PROBE_H
#define DSPET_DUPLEX_PROBE_H

#include "esp_err.h"

/**
 * @brief 启动双路音频诊断任务。
 * @return ESP_OK 表示已启动，其余返回资源或音频设备错误。
 * @note 应用启动播放服务后、提交首段音频前调用。串口只记录统计值。
 */
esp_err_t DSPetDuplexProbeStart(void);

#endif
