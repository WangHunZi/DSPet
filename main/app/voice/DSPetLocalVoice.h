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
 * @brief 读取当前播报画面所对应的回合。
 * @return 非零播报回合编号，其余状态返回零。
 */
uint32_t DSPetLocalVoiceGetSpeakingTurn(void);

/**
 * @brief 通知语音任务，该回合的播报表情首帧已经完成面板传输。
 * @param[in] turnId 开始绘制时读取的非零回合编号。
 * @note 由显示任务调用。旧回合的通知不能启动当前回合。
 */
void DSPetLocalVoiceSpeakingFramePresented(uint32_t turnId);

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
 * @brief 请求切换免唤醒。关闭时取消当前回合并回到唤醒待机。
 * @param[in] enabled true 开启，false 关闭。
 * @return ESP_OK 表示已提交；直连未就绪或试图在忙碌时开启，返回 ESP_ERR_INVALID_STATE。
 * @note 仅在当前直连会话有效，语音任务异步应用。
 */
esp_err_t DSPetLocalVoiceSetAlwaysListening(bool enabled);

/** @brief 已请求开启免唤醒时返回 true。 */
bool DSPetLocalVoiceAlwaysListeningRequested(void);

/**
 * @brief 用本地 PCM 验证直连待机、免唤醒聆听或播报插话。
 * @return ESP_OK 表示已提交；离线、等待或未就绪时返回状态错误。
 * @note 免唤醒期间已有录音时，语音任务丢弃此请求。
 */
esp_err_t DSPetLocalVoiceStartFixture(void);
#endif

#endif
