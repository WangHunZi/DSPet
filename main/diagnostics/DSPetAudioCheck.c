#include "DSPetAudioCheck.h"
#include "DSPetAudioDevice.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdlib.h>

enum {
    kIoTimeoutMs = 250,
    kToneMs = 280,
    kTonePeak = 768,
    kRecordSeconds = 3,
    kRecordSamples = kDSPetAudioDeviceSampleRateHz * kRecordSeconds,
    kReplayPeak = 2048,
    kGainScale = 256,
};

static const char *const kTag = "audio_check";

static esp_err_t playTone(unsigned hz)
{
    const unsigned samples = kDSPetAudioDeviceSampleRateHz * kToneMs / 1000;
    const unsigned fade = kDSPetAudioDeviceSampleRateHz / 100;
    const float phaseStep = 6.28318530718f * hz / kDSPetAudioDeviceSampleRateHz;
    int16_t chunk[kDSPetAudioDeviceBlockSamples];
    for (unsigned offset = 0; offset < samples;) {
        unsigned count = samples - offset;
        if (count > kDSPetAudioDeviceBlockSamples) {
            count = kDSPetAudioDeviceBlockSamples;
        }
        for (unsigned i = 0; i < count; ++i) {
            unsigned n = offset + i;
            unsigned edge = n < samples - 1 - n ? n : samples - 1 - n;
            float envelope = edge < fade ? (float)edge / fade : 1.0f;
            chunk[i] = (int16_t)(kTonePeak * envelope * sinf(phaseStep * n));
        }
        ESP_RETURN_ON_ERROR(DSPetAudioDeviceWrite(chunk, count, kIoTimeoutMs), kTag, "play tone");
        offset += count;
    }
    return DSPetAudioDeviceDrain(kIoTimeoutMs);
}

static esp_err_t capture(int16_t *pcm, size_t sampleCount)
{
    ESP_RETURN_ON_ERROR(DSPetAudioDeviceStartCapture(), kTag, "enable microphone RX");
    esp_err_t result = ESP_OK;
    for (size_t offset = 0; offset < sampleCount;) {
        size_t count = sampleCount - offset;
        if (count > kDSPetAudioDeviceBlockSamples) {
            count = kDSPetAudioDeviceBlockSamples;
        }
        result = DSPetAudioDeviceRead(pcm + offset, count, kIoTimeoutMs);
        if (result != ESP_OK) {
            break;
        }
        offset += count;
    }
    esp_err_t stop = DSPetAudioDeviceStopCapture();
    return result == ESP_OK ? stop : result;
}

static esp_err_t replayCapture(const int16_t *pcm, size_t samples)
{
    int64_t sum = 0;
    for (size_t i = 0; i < samples; ++i) sum += pcm[i];
    const int32_t dc = (int32_t)(sum / (int64_t)samples);
    int32_t peak = 0;
    int64_t energy = 0;
    for (size_t i = 0; i < samples; ++i) {
        int32_t sample = pcm[i] - dc;
        int32_t magnitude = abs(sample);
        if (magnitude > peak) {
            peak = magnitude;
        }
        energy += (int64_t)sample * sample;
    }
    ESP_LOGI(kTag, "Mic captured %u samples, DC=%ld AC peak=%ld RMS=%.1f (PCM16)",
             (unsigned)samples, (long)dc, (long)peak, sqrt((double)energy / samples));
    if (peak == 0) {
        ESP_LOGW(kTag, "No variation in left slot; check microphone power, SD and L/R");
        return ESP_OK;
    }

    // 限制播放峰值，也限制安静环境下的增益。
    int32_t gain = kReplayPeak * kGainScale / peak;
    if (gain > 16 * kGainScale) {
        gain = 16 * kGainScale;
    }
    ESP_LOGI(kTag, "Replaying recording with peak capped at %d/32768", kReplayPeak);
    int16_t chunk[kDSPetAudioDeviceBlockSamples];
    for (size_t offset = 0; offset < samples;) {
        size_t count = samples - offset;
        if (count > kDSPetAudioDeviceBlockSamples) {
            count = kDSPetAudioDeviceBlockSamples;
        }
        for (size_t i = 0; i < count; ++i) {
            chunk[i] = (int16_t)((pcm[offset + i] - dc) * gain / kGainScale);
        }
        ESP_RETURN_ON_ERROR(DSPetAudioDeviceWrite(chunk, count, kIoTimeoutMs), kTag, "play captured audio");
        offset += count;
    }
    return DSPetAudioDeviceDrain(kIoTimeoutMs);
}

static esp_err_t checkMicrophone(void)
{
    int16_t *pcm = malloc(kRecordSamples * sizeof(*pcm));
    if (!pcm) {
        return ESP_ERR_NO_MEM;
    }
    // 提示音结束并留出回声衰减时间后再录音。
    vTaskDelay(pdMS_TO_TICKS(350));
    ESP_LOGI(kTag, "Recording the next %d seconds (left slot)", kRecordSeconds);
    esp_err_t result = capture(pcm, kRecordSamples);
    if (result == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(250));
        result = replayCapture(pcm, kRecordSamples);
    }
    free(pcm);
    return result;
}

void DSPetAudioCheckRun(void)
{
    esp_err_t result = DSPetAudioDeviceOpen();
    if (result == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(350));
        ESP_LOGI(kTag, "Speaker test: 750 Hz, PCM peak %d/32768", kTonePeak);
        result = playTone(750);
        if (result == ESP_OK) {
            ESP_LOGI(kTag, "Speaker test frames sent");
            result = checkMicrophone();
        }
        esp_err_t closeResult = DSPetAudioDeviceClose();
        if (result == ESP_OK) {
            result = closeResult;
        }
    }
    if (result == ESP_OK) {
        ESP_LOGI(kTag, "Audio check finished");
    } else {
        ESP_LOGW(kTag, "Audio check failed: %s", esp_err_to_name(result));
    }
}
