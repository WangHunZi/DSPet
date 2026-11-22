#ifndef DSPET_SETUP_FORM_H
#define DSPET_SETUP_FORM_H

#include <stdbool.h>

/** @brief 配置页提交并持久化的一组凭据。长度含结尾 NUL。 */
typedef struct {
    char ssid[33];     /**< 1 至 32 字节的 2.4 GHz 网络名称。 */
    char password[65]; /**< WPA2 或更新类型的 Wi-Fi 口令。 */
    char key[129];     /**< DeepSeek API Key。 */
} DSPetSetupConfig;

/**
 * @brief 校验来自网页或 Flash 的凭据。
 * @param[in] config 完整配置。
 * @return 各字段都有终止符且符合范围时返回 true。
 */
bool DSPetSetupConfigValid(const DSPetSetupConfig *config);

/**
 * @brief 解析 application/x-www-form-urlencoded 配置表单。
 * @param[in] body 以 NUL 结尾的请求体。
 * @param[in] token 当前页面的随机表单标记。
 * @param[out] config 接收配置。失败时调用方须丢弃内容。
 * @return 字段完整、标记匹配且配置有效时返回 true。
 */
bool DSPetSetupFormParse(const char *body, const char *token, DSPetSetupConfig *config);

#endif
