#ifndef DSPET_CONSOLE_SECRET_H
#define DSPET_CONSOLE_SECRET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 在 USB 串口提示后读取一行凭据，不通过回显模块。
 * @param[in] field 用于提示的字段名。
 * @param[out] value 输出缓冲，成功时以 NUL 结尾。
 * @param[in] capacity 字节容量，包含终止符。
 * @return 成功为 true，取消或无效输入为 false。
 * @note 由串口任务在命令处理期间调用，调用方负责擦除。
 */
bool DSPetConsoleReadSecret(const char *field, char *value, size_t capacity);

/**
 * @brief 校验串口传入的 Unix 时间。
 * @param[in] text 秒数十进制字符串。
 * @param[out] unixTime 成功时获得经过校验的秒数。
 * @return 格式和范围有效时为 true。
 */
bool DSPetConsoleParseUnixTime(const char *text, uint32_t *unixTime);

#endif
