#ifndef DSPET_SETUP_PORTAL_H
#define DSPET_SETUP_PORTAL_H

#include <stdbool.h>

#include "esp_err.h"

#define DSPET_SETUP_PORTAL_ADDRESS "192.168.4.1"

/** @brief 仅在临时热点开启期间有效的入网信息。 */
typedef struct {
    char ssid[13];     /**< DSPet 加本轮随机后缀。 */
    char password[17]; /**< 本轮生成的随机 WPA2 口令。 */
} DSPetSetupHotspot;

/**
 * @brief 启动临时热点和配置网页。
 * @param[out] hotspot 成功时写入用于展示的临时入网信息。
 * @return ESP_OK 表示成功，其余为网络或 HTTP 服务错误。
 * @note 由应用任务独占。配网结束前须调用 DSPetSetupPortalStop。
 */
esp_err_t DSPetSetupPortalStart(DSPetSetupHotspot *hotspot);

/** @brief 处理少量本地 DNS 请求，返回表单是否已保存且网页响应已提交。 */
bool DSPetSetupPortalPoll(void);

/** @brief 等 HTTP 服务停止，随后释放热点、DNS 和临时标记。 */
void DSPetSetupPortalStop(void);

#endif
