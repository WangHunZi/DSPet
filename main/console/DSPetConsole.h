#ifndef DSPET_CONSOLE_H
#define DSPET_CONSOLE_H

#include "esp_err.h"

/**
 * @brief 注册 dspet 命令并启动 USB Serial/JTAG 控制台任务。
 * @return ESP_OK 表示运行，失败时透传 ESP-IDF Console 错误。
 * @note 从启动任务调用一次。
 */
esp_err_t DSPetConsoleStart(void);

#endif
