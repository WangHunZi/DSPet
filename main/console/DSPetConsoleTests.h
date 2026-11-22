#ifndef DSPET_CONSOLE_TESTS_H
#define DSPET_CONSOLE_TESTS_H

#include <stdbool.h>

#include "esp_err.h"

/**
 * @brief 启动测试任务和 PCM 播放服务。
 * @return ESP_OK 表示可接受测试命令，其余表示资源初始化失败。
 */
esp_err_t DSPetConsoleTestsInit(void);

/**
 * @brief 处理 dspet test 子命令。
 * @param[in] argc test 之后的参数数。
 * @param[in] argv test 之后的参数向量。
 * @return 0 表示请求接受，1 表示参数或状态错误。
 * @note 由串口命令任务调用。耗时测试交给测试任务。
 */
int DSPetConsoleTestsRunCommand(int argc, char **argv);

/** @brief 显示已注册的测试子命令。 */
void DSPetConsoleTestsHelp(void);

/**
 * @brief 查询诊断任务是否正在使用音频设备。
 * @return 有测试运行时为 true，等待人工确认时为 false。
 */
bool DSPetConsoleTestsAudioBusy(void);

#endif
