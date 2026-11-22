#ifndef DSPET_DIRECT_VOICE_H
#define DSPET_DIRECT_VOICE_H

#include "DSPetDeepSeek.h"
#include "DSPetWifi.h"

/** @brief 仅在当前串口直连会话期间使用的配置。 */
typedef struct {
    char     ssid[kDSPetWifiSsidBytes + 1];         /**< 已知的 2.4 GHz 网络。 */
    char     password[kDSPetWifiPasswordBytes + 1]; /**< Wi-Fi 口令。 */
    char     key[kDSPetDeepSeekKeyBytes + 1];       /**< DeepSeek 临时凭据。 */
    uint32_t unixTime;                              /**< USB 主机提供的时间，零表示用 SNTP 同步。 */
} DSPetDirectSettings;

/** @brief 独占板载网络的语音服务。 */
typedef struct DSPetDirectVoice DSPetDirectVoice;

/** @brief 当前回合的网络与缓冲状态。 */
typedef struct {
    uint32_t  turnId;   /**< 当前持有缓冲的回合号，未提交时为零。 */
    bool      ready;    /**< 达到预缓冲门限或已收到较短的完整回复。 */
    bool      complete; /**< 网络任务已结束并释放 HTTPS 连接。 */
    esp_err_t result;   /**< complete 时有效，ESP_ERR_NOT_FOUND 表示无有效转写或模型选择忽略。 */
} DSPetDirectVoiceStatus;

/**
 * @brief 联网、配置时钟并准备直连任务。
 * @param[in] settings 输入配置，返回前已复制。
 * @param[out] voice 成功时写入实例，失败时写入 NULL。
 * @return ESP_OK 表示实例就绪，其余为分配、时钟或 Wi-Fi 错误。
 * @note 会话任务调用，可能等待 Wi-Fi 连接。
 */
esp_err_t DSPetDirectVoiceCreate(const DSPetDirectSettings *settings, DSPetDirectVoice **voice);

/**
 * @brief 复制当前录音并提交整轮 ASR、对话和流式合成。
 * @param[in,out] voice 已创建的实例。
 * @param[in] turnId 非零回合编号。
 * @param[in] samples 16 kHz 单声道 PCM16。
 * @param[in] sampleCount 样本数，在板载单句容量以内。
 * @param[in] newSession 新会话时清除上一轮上下文。
 * @param[in] allowIgnore 为 true 时，允许模型跳过当前录音。
 * @return ESP_OK 表示已提交，ESP_ERR_INVALID_STATE 表示上一轮仍在收尾。
 * @note 会话任务须先确认旧播放已到终态，录音在返回后可复用。
 */
esp_err_t DSPetDirectVoiceSubmit(DSPetDirectVoice *voice,
                                 uint32_t          turnId,
                                 const int16_t    *samples,
                                 size_t            sampleCount,
                                 bool              newSession,
                                 bool              allowIgnore);

/**
 * @brief 读取当前回合的状态，供会话任务轮询。
 * @param[in] voice 已创建的实例。
 * @return 当前回合及预缓冲条件。
 */
DSPetDirectVoiceStatus DSPetDirectVoiceGetStatus(DSPetDirectVoice *voice);

/**
 * @brief 从当前回合的缓冲读取 PCM，作为播放流读取回调。
 * @param[in,out] context 已创建的 DSPetDirectVoice。
 * @param[out] samples PCM 输出缓冲。
 * @param[in] capacitySamples 缓冲容量，单位为样本。
 * @param[out] sampleCount 成功时写入输出样本数，零表示播放流结束。
 * @return ESP_OK 表示成功，ESP_ERR_TIMEOUT 表示暂时无数据，或返回终态错误。
 */
esp_err_t DSPetDirectVoiceRead(void *context, int16_t *samples, size_t capacitySamples, size_t *sampleCount);

/**
 * @brief 取消当前回合。由网络任务停止继续向播放缓冲写入并关闭 HTTPS。
 * @param[in,out] voice 已创建的实例。
 * @param[in] turnId 要取消的回合号。
 */
void DSPetDirectVoiceCancel(DSPetDirectVoice *voice, uint32_t turnId);

/**
 * @brief 等网络任务退出、释放无线设备并擦除会话凭据。
 * @param[in,out] voice 已创建的实例，可为 NULL。
 * @note 会话任务在收到播放终态后调用。
 */
void DSPetDirectVoiceDestroy(DSPetDirectVoice *voice);

#endif
