#ifndef DSPET_LOCAL_VOICE_H
#define DSPET_LOCAL_VOICE_H

#include "DSPetVoiceSession.h"
#include "esp_err.h"
#include "sdkconfig.h"

#ifdef CONFIG_DSPET_DIRECT_TEST
#include "DSPetDirectVoice.h"
#endif

/** @brief 语音任务占用音频设备的状态。 */
typedef enum {
    kDSPetLocalVoiceOff,      /**< 音频设备已释放，可运行诊断。 */
    kDSPetLocalVoiceStarting, /**< 正在准备模型和音频设备。 */
    kDSPetLocalVoiceOn,       /**< 唤醒和会话可用。 */
    kDSPetLocalVoiceStopping, /**< 正在排空播放并释放音频设备。 */
} DSPetLocalVoiceMode;

/**
 * @brief 启动本地会话任务和共用播放服务，默认开启唤醒。
 * @return ESP_OK 表示任务已启动，模型和设备的准备结果由运行状态报告。
 */
esp_err_t DSPetLocalVoiceInit(void);

/**
 * @brief 请求开启或关闭唤醒和会话。关闭后允许独立音频诊断。
 * @param[in] enabled true 请求开启，false 请求释放音频设备。
 * @return ESP_OK 表示请求适用或已处于该模式，反向过渡未完成时返回状态错误。
 */
esp_err_t DSPetLocalVoiceSetEnabled(bool enabled);

/**
 * @brief 手动启动一轮聆听，供 BOOT 与板载串口调用。
 * @return ESP_OK 表示已提交，局部会话已开始或功能未就绪时返回状态错误。
 */
esp_err_t DSPetLocalVoiceStart(void);

/**
 * @brief 查询当前语音任务的设备占用状态。
 * @return 功能关闭、准备、启用或释放中。
 */
DSPetLocalVoiceMode DSPetLocalVoiceGetMode(void);

/**
 * @brief 读取供界面和串口使用的会话状态快照。
 * @return 最近公布的会话状态。
 */
DSPetVoiceState DSPetLocalVoiceGetState(void);

/**
 * @brief 获取会话状态的日志名称。
 * @param[in] state 会话状态。
 * @return 生命周期内有效的英文状态名。
 */
const char *DSPetLocalVoiceStateName(DSPetVoiceState state);

#ifdef CONFIG_DSPET_DIRECT_TEST
/**
 * @brief 在语音设备关闭时以直连提供方启动会话。
 * @param[in] settings 输入配置，返回前已复制，停用时擦除。
 * @return ESP_OK 表示已发起，ESP_ERR_INVALID_STATE 表示语音任务尚未释放。
 */
esp_err_t DSPetLocalVoiceEnableDirect(const DSPetDirectSettings *settings);

/** @brief 直连正在启动或持有 Wi-Fi 时返回 true。 */
bool DSPetLocalVoiceDirectActive(void);

/**
 * @brief 在直连待机或播报时用本地 PCM 验证真实网络回合或受控插话。
 * @return ESP_OK 表示已提交；离线、聆听、等待或未就绪时返回状态错误。
 */
esp_err_t DSPetLocalVoiceStartFixture(void);
#endif

#endif
