#ifndef DSPET_WIFI_H
#define DSPET_WIFI_H

#include <stdbool.h>

#include "esp_err.h"
#include "sdkconfig.h"

enum {
    kDSPetWifiSsidBytes     = 32,
    kDSPetWifiPasswordBytes = 64,
};

/**
 * @brief 打开 2.4 GHz STA，初始化本地网络栈。
 * @return ESP_OK 表示就绪，其余为 ESP-IDF 驱动错误。
 * @note 由当前网络任务独占，成功后须调用 DSPetWifiClose。
 */
esp_err_t DSPetWifiOpen(void);

#ifdef CONFIG_DSPET_PROVISIONING
/**
 * @brief 打开使用 WPA2 的临时配置热点。
 * @param[in] ssid 非空接入点名称，最多 32 字节。
 * @param[in] password 8 至 63 字节的临时口令。
 * @return ESP_OK 表示热点可用，其余为参数、占用或驱动错误。
 * @note 与 STA 互斥，同一所有者调用 DSPetWifiClose 释放。
 */
esp_err_t DSPetWifiOpenAccessPoint(const char *ssid, const char *password);
#endif

/**
 * @brief 在 STA 已取得 IP 后尝试通过 SNTP 校正 UTC 时间。
 * @return ESP_OK 表示同步完成，其余为初始化或超时错误。
 * @note 最多等待 12 秒。
 */
esp_err_t DSPetWifiSyncTime(void);

/**
 * @brief 扫描接入点并打印匹配前缀的 SSID、信道与信号强度。
 * @param[in] prefix 只列出与此前缀匹配的网络。
 * @return ESP_OK 表示找到匹配，ESP_ERR_NOT_FOUND 表示在扫描中未找到。
 */
esp_err_t DSPetWifiScan(const char *prefix);

/**
 * @brief 连接指定的 WPA2 或更高安全级别的网络，等待 DHCP。
 * @param[in] ssid 接入点名称，最多 32 字节。
 * @param[in] password 此次连接的口令，最多 64 字节。
 * @return ESP_OK 表示已取得 IP，其余为连接、驱动或超时错误。
 * @note Wi-Fi 驱动使用 RAM 存储。调用期间借用凭据，关闭驱动时释放配置。
 */
esp_err_t DSPetWifiConnect(const char *ssid, const char *password);

/** @brief 查询当前 STA 是否仍持有可用的网络连接和 IP。 */
bool DSPetWifiIsConnected(void);

/**
 * @brief 断开并释放当前接口、事件回调及驱动资源。
 * @note 同一网络任务调用，关闭后还可重新打开。
 */
void DSPetWifiClose(void);

#endif
