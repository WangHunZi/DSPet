#ifndef DSPET_AUDIO_DEVICE_H
#define DSPET_AUDIO_DEVICE_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

enum {
    kDSPetAudioDeviceSampleRateHz = 16000,
    kDSPetAudioDeviceBlockSamples = 128,
};

/**
 * @brief 初始化共享 I²S 时钟，启用 TX 并保持送时钟。
 * @return ESP_OK 表示成功，其余返回状态错误或 I²S 驱动错误。
 * @note 启停函数由掌管设备生命周期的任务调用。采集初始关闭。
 */
esp_err_t DSPetAudioDeviceOpen(void);

/**
 * @brief 关闭并释放 I²S 设备。
 * @return ESP_OK 表示资源已释放，其余返回 I²S 驱动错误。
 * @note 调用前结束所有读写。设备未打开也可调用。
 */
esp_err_t DSPetAudioDeviceClose(void);

/**
 * @brief 启用麦克风采集，TX 继续产生共享时钟。
 * @return ESP_OK 表示成功，其余返回状态错误或 I²S 驱动错误。
 */
esp_err_t DSPetAudioDeviceStartCapture(void);

/**
 * @brief 停止麦克风采集。
 * @return ESP_OK 表示采集已停止，其余返回 I²S 驱动错误。
 * @note 在当前读操作返回后调用。
 */
esp_err_t DSPetAudioDeviceStopCapture(void);

/**
 * @brief 从麦克风读取一块 16 kHz 单声道 PCM16。
 * @param[out] samples 能容纳 sampleCount 个样本的输出缓冲。
 * @param[in] sampleCount 样本数，范围为 1 到 kDSPetAudioDeviceBlockSamples。
 * @param[in] timeoutMs 等待 I²S 数据的超时参数，单位毫秒。
 * @return ESP_OK 表示写满本块。错误时调用方丢弃本块。
 * @note 启动采集后由单个读任务调用。允许一个独立写任务同时运行。
 */
esp_err_t DSPetAudioDeviceRead(int16_t *samples, size_t sampleCount, uint32_t timeoutMs);

/**
 * @brief 向喇叭排入一块 16 kHz 单声道 PCM16。
 * @param[in] samples 输入音频，调用期间有效。
 * @param[in] sampleCount 样本数，范围为 1 到 kDSPetAudioDeviceBlockSamples。
 * @param[in] timeoutMs 等待 I²S 发送空间的超时参数，单位毫秒。
 * @return ESP_OK 表示本块已排入 DMA。出错时可能已排入部分样本。
 * @note 打开设备后由单个写任务调用。返回成功时源缓冲可复用，扬声器可能仍在播放。
 */
esp_err_t DSPetAudioDeviceWrite(const int16_t *samples, size_t sampleCount, uint32_t timeoutMs);

/**
 * @brief 根据 TX DMA 完成通知确认已写入的音频通过输出链路。
 * @param[in] timeoutMs 等待上限，单位毫秒，须大于零。
 * @return ESP_OK 表示排空。无完成通知时返回 ESP_ERR_TIMEOUT。
 * @note 由唯一的写任务在最后一次写入后调用，期间继续输出时钟和静音。
 */
esp_err_t DSPetAudioDeviceDrain(uint32_t timeoutMs);

#endif
