#ifndef DSPET_PLAYBACK_H
#define DSPET_PLAYBACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** @brief 播放任务向应用发布的状态变化。 */
typedef enum {
    kDSPetPlaybackEventQueued,    /**< 第一块 PCM 已排入 DMA。 */
    kDSPetPlaybackEventFinished,  /**< 片段自然结束，输出已排空。 */
    kDSPetPlaybackEventCancelled, /**< 已停止追加旧样本，输出已排空。 */
    kDSPetPlaybackEventFailed,    /**< 播放或排空失败，详情见 error。 */
} DSPetPlaybackEventKind;

/** @brief 一次播放的通知。 */
typedef struct {
    DSPetPlaybackEventKind kind;       /**< 当前通知类型。 */
    esp_err_t              error;      /**< 失败时的底层错误，其余为 ESP_OK。 */
    int64_t                occurredUs; /**< 播放任务发出事件时的单调时钟，单位微秒。 */
} DSPetPlaybackEvent;

/**
 * @brief 由播放任务读取一块 16 kHz 单声道 PCM16。
 * @param[in,out] context 直到播放终态有效的源状态。
 * @param[out] samples PCM 输出缓冲。
 * @param[in] capacitySamples 缓冲容量，单位为样本。
 * @param[out] sampleCount 成功时写入本次样本数，零表示流结束。
 * @return ESP_OK 表示收到数据或流结束，ESP_ERR_TIMEOUT 表示暂时无数据，其余错误终止播放。
 * @note 每次等待不得超过 10 ms，以便播放任务及时处理取消。
 */
typedef esp_err_t (*DSPetPlaybackRead)(void *context, int16_t *samples, size_t capacitySamples, size_t *sampleCount);

/**
 * @brief 启动 PCM 播放任务。
 * @return ESP_OK 表示任务就绪，其余返回初始化错误。
 * @note 同一调用任务使用公开接口。设备由调用方管理，初始化任务时可以尚未打开。
 */
esp_err_t DSPetPlaybackInit(void);

/**
 * @brief 提交一段 16 kHz 单声道 PCM16。
 * @param[in] samples 非空 PCM 缓冲，保持有效直到收到结束、取消或失败通知。
 * @param[in] sampleCount 样本数，须大于零。
 * @return ESP_OK 表示请求入队。已有一轮未完成时返回 ESP_ERR_INVALID_STATE。
 * @note 提交前打开音频设备，取到终态后才可关闭。
 */
esp_err_t DSPetPlaybackStart(const int16_t *samples, size_t sampleCount);

/**
 * @brief 提交一个分块读取的音频源，复用相同的 DMA 播放与排空事件。
 * @param[in] read 非空的流读取回调。
 * @param[in,out] context 传给回调，保持有效直到收到终态。
 * @return ESP_OK 表示已入队，其余为参数或播放忙错误。
 */
esp_err_t DSPetPlaybackStartStream(DSPetPlaybackRead read, void *context);

/**
 * @brief 请求取消当前播放。
 * @return 当前有待处理的播放时返回 true。
 * @note 调用立即返回。播放任务停止送入旧音频，等 DMA 尾部排空后报告结果。
 */
bool DSPetPlaybackCancel(void);

/**
 * @brief 非阻塞读取一条状态通知。
 * @param[out] event 有新通知时写入结果。
 * @return 读到通知时返回 true。
 * @note 读到结束、取消或失败通知后，应用才可开始下一轮或释放音频。
 */
bool DSPetPlaybackTakeEvent(DSPetPlaybackEvent *event);

#endif
