#include "DSPetAudioDevice.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"

enum { kDmaBlocks = 4, kMicDataPin = GPIO_NUM_44 };

static const char *const kTag = "audio_device";
static i2s_chan_handle_t sTx;
static i2s_chan_handle_t sRx;
static bool sTxActive;
static bool sRxActive;
// 收发各有独立的槽缓冲，调用方可以分别从读写任务使用。
static int32_t sTxSlots[kDSPetAudioDeviceBlockSamples][2];
static int32_t sRxSlots[kDSPetAudioDeviceBlockSamples][2];

static esp_err_t prepareChannels(void)
{
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    channel.dma_frame_num = kDSPetAudioDeviceBlockSamples;
    channel.dma_desc_num = kDmaBlocks;
    channel.auto_clear_after_cb = true;
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kDSPetAudioDeviceSampleRateHz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = GPIO_NUM_1,  // D0，BCLK/SCK
            .ws = GPIO_NUM_2,    // D1，LRC/WS
            .dout = GPIO_NUM_43, // D6，MAX98357A DIN
            .din = kMicDataPin,  // D7，INMP441 SD
        },
    };
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel, &sTx, &sRx), kTag, "allocate I2S channels");
    // 先初始化 TX，由 TX 提供共享时钟。
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(sTx, &config), kTag, "init TX");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(sRx, &config), kTag, "init RX");
    return gpio_pulldown_en(kMicDataPin);
}

esp_err_t DSPetAudioDeviceOpen(void)
{
    if (sTx || sRx) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = prepareChannels();
    if (error == ESP_OK) {
        error = i2s_channel_enable(sTx);
    }
    if (error == ESP_OK) {
        sTxActive = true;
        ESP_LOGI(kTag, "I2S Philips 16 kHz, 2 x 32-bit, BCLK=1 WS=2 DOUT=43 DIN=44");
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(DSPetAudioDeviceClose());
    }
    return error;
}

esp_err_t DSPetAudioDeviceStartCapture(void)
{
    if (!sTxActive || sRxActive) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = i2s_channel_enable(sRx);
    if (error == ESP_OK) {
        sRxActive = true;
    }
    return error;
}

esp_err_t DSPetAudioDeviceStopCapture(void)
{
    if (!sRxActive) {
        return ESP_OK;
    }
    esp_err_t error = i2s_channel_disable(sRx);
    if (error == ESP_OK) {
        sRxActive = false;
    }
    return error;
}

esp_err_t DSPetAudioDeviceClose(void)
{
    ESP_RETURN_ON_ERROR(DSPetAudioDeviceStopCapture(), kTag, "stop RX");
    if (sTxActive) {
        ESP_RETURN_ON_ERROR(i2s_channel_disable(sTx), kTag, "stop TX");
        sTxActive = false;
    }
    if (sRx) {
        ESP_RETURN_ON_ERROR(i2s_del_channel(sRx), kTag, "delete RX");
        sRx = NULL;
    }
    if (sTx) {
        ESP_RETURN_ON_ERROR(i2s_del_channel(sTx), kTag, "delete TX");
        sTx = NULL;
    }
    return ESP_OK;
}

esp_err_t DSPetAudioDeviceRead(int16_t *samples, size_t sampleCount, uint32_t timeoutMs)
{
    if (!sRxActive) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!samples || !sampleCount || sampleCount > kDSPetAudioDeviceBlockSamples) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t receivedBytes = 0;
    const size_t sizeBytes = sampleCount * sizeof(sRxSlots[0]);
    ESP_RETURN_ON_ERROR(i2s_channel_read(sRx, sRxSlots, sizeBytes, &receivedBytes, timeoutMs), kTag, "read PCM");
    if (receivedBytes != sizeBytes) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t i = 0; i < sampleCount; ++i) {
        // INMP441 的 L/R 接地。取左槽中 24 位有效数据的高 16 位。
        samples[i] = (int16_t)(sRxSlots[i][0] >> 16);
    }
    return ESP_OK;
}

esp_err_t DSPetAudioDeviceWrite(const int16_t *samples, size_t sampleCount, uint32_t timeoutMs)
{
    if (!sTxActive) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!samples || !sampleCount || sampleCount > kDSPetAudioDeviceBlockSamples) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < sampleCount; ++i) {
        // 左右槽送相同数据，兼容功放的通道选择。
        sTxSlots[i][0] = sTxSlots[i][1] = (int32_t)samples[i] * 65536;
    }
    size_t writtenBytes = 0;
    const size_t sizeBytes = sampleCount * sizeof(sTxSlots[0]);
    ESP_RETURN_ON_ERROR(i2s_channel_write(sTx, sTxSlots, sizeBytes, &writtenBytes, timeoutMs), kTag, "write PCM");
    return writtenBytes == sizeBytes ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

unsigned DSPetAudioDeviceTxBufferMs(void)
{
    return (kDmaBlocks * kDSPetAudioDeviceBlockSamples * 1000 + kDSPetAudioDeviceSampleRateHz - 1) /
           kDSPetAudioDeviceSampleRateHz;
}
