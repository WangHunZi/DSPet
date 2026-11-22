#ifndef DSPET_SETUP_STORE_H
#define DSPET_SETUP_STORE_H

#include "DSPetSetupForm.h"
#include "esp_err.h"

/** @brief 实际设备配置使用的 NVS 命名空间。 */
extern const char kDSPetSetupNamespace[];

/**
 * @brief 读取已保存的一组配置。
 * @param[in] space NVS 命名空间。
 * @param[out] config 成功时写入凭据。
 * @return ESP_OK 表示读取有效配置，ESP_ERR_NVS_NOT_FOUND 表示未配置，其余为存储或记录错误。
 */
esp_err_t DSPetSetupStoreLoad(const char *space, DSPetSetupConfig *config);

/**
 * @brief 将一组凭据作为一条记录写入 NVS 并提交。
 * @param[in] space NVS 命名空间。
 * @param[in] config 输入配置，调用期间有效。
 * @return ESP_OK 表示写入并提交成功，其余为参数或存储错误。
 */
esp_err_t DSPetSetupStoreSave(const char *space, const DSPetSetupConfig *config);

/**
 * @brief 擦除指定配网命名空间的凭据及旧记录，并提交。
 * @param[in] space NVS 命名空间。
 * @return ESP_OK 表示已提交清除，或原来未配置，其余为存储错误。
 */
esp_err_t DSPetSetupStoreClear(const char *space);

#endif
