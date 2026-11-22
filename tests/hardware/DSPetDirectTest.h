#ifndef DSPET_DIRECT_TEST_H
#define DSPET_DIRECT_TEST_H

#include "esp_err.h"

/**
 * @brief 设置下一轮扫描的前缀，或下一轮直连的临时配置。
 * @param[in] ssid 扫描前缀或完整 SSID。
 * @param[in] password Wi-Fi 口令。扫描时传 NULL。
 * @param[in] key DeepSeek 密钥。扫描时传 NULL。
 * @return ESP_OK 表示已准备，其余为参数错误。
 * @note 串口任务在测试空闲时调用，测试结束后擦除配置。
 */
esp_err_t DSPetDirectTestPrepare(const char *ssid, const char *password, const char *key);

/** @brief 擦除本轮临时网络配置。 */
void DSPetDirectTestClear(void);

/**
 * @brief 打开 2.4 GHz 无线网卡并报告匹配的接入点。
 * @return ESP_OK 表示找到匹配项，其余为扫描或驱动错误。
 */
esp_err_t DSPetDirectTestScan(void);

/**
 * @brief 用临时配置连接 Wi-Fi，直接请求并播放固定问题的回复。
 * @return ESP_OK 表示完整接收和排空，其他值为底层错误。
 */
esp_err_t DSPetDirectTestRun(void);

#endif
