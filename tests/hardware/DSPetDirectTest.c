#include "DSPetDirectTest.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "DSPetAudioDevice.h"
#include "DSPetDeepSeek.h"
#include "DSPetPcm24kTo16k.h"
#include "DSPetWifi.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "mbedtls/platform_util.h"

enum {
    kWriteTimeoutMs     = 40,
    kDrainTimeoutMs     = 120,
    kSpeakerAttenuation = 10,
    kReplySamples       = 20 * kDSPetAudioDeviceSampleRateHz
};

static struct {
    char     ssid[kDSPetWifiSsidBytes + 1];
    char     password[kDSPetWifiPasswordBytes + 1];
    char     key[kDSPetDeepSeekKeyBytes + 1];
    uint32_t unixTime;
    bool     shortReply;
} sSettings;

esp_err_t
DSPetDirectTestPrepare(const char *ssid, const char *password, const char *key, uint32_t unixTime, bool shortReply) {
    const size_t ssidBytes     = strlen(ssid);
    const size_t passwordBytes = password ? strlen(password) : 0;
    const size_t keyBytes      = key ? strlen(key) : 0;
    if (!ssidBytes || ssidBytes > kDSPetWifiSsidBytes ||
        (password && (passwordBytes < 8 || passwordBytes > kDSPetWifiPasswordBytes)) ||
        (key && (!keyBytes || keyBytes > kDSPetDeepSeekKeyBytes))) {
        return ESP_ERR_INVALID_ARG;
    }
    DSPetDirectTestClear();
    sSettings.unixTime   = unixTime;
    sSettings.shortReply = shortReply;
    memcpy(sSettings.ssid, ssid, ssidBytes);
    if (password) {
        memcpy(sSettings.password, password, passwordBytes);
    }
    if (key) {
        memcpy(sSettings.key, key, keyBytes);
    }
    return ESP_OK;
}

void DSPetDirectTestClear(void) { mbedtls_platform_zeroize(&sSettings, sizeof(sSettings)); }

static void printHeap(const char *stage) {
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    printf("DSPET_DIRECT_HEAP stage=%s free_internal=%zu largest_internal=%zu min_internal=%zu\n",
           stage,
           heap_caps_get_free_size(caps),
           heap_caps_get_largest_free_block(caps),
           heap_caps_get_minimum_free_size(caps));
}

esp_err_t DSPetDirectTestScan(void) {
    printHeap("before_wifi");
    esp_err_t error = DSPetWifiOpen();
    if (error == ESP_OK) {
        printHeap("wifi_scan");
        error = DSPetWifiScan(sSettings.ssid);
        DSPetWifiClose();
    }
    DSPetDirectTestClear();
    return error;
}

typedef struct {
    DSPetPcm24kTo16k converter;
    int16_t         *pcm;
    size_t           samples;
} AudioPreview;

static esp_err_t consumePcm(const uint8_t *bytes, size_t count, void *context) {
    AudioPreview *output = context;
    enum { kDecodeBytes = 256 };
    while (count) {
        int16_t      block[kDecodeBytes / 2 + 2];
        const size_t take    = count > kDecodeBytes ? kDecodeBytes : count;
        const size_t samples = DSPetPcm24kTo16kDecode(&output->converter, bytes, take, block);
        if (samples > kReplySamples - output->samples) {
            return ESP_ERR_INVALID_SIZE;
        }
        for (size_t i = 0; i < samples; ++i) {
            output->pcm[output->samples++] = block[i] / kSpeakerAttenuation;
        }
        bytes += take;
        count -= take;
    }
    return ESP_OK;
}

static esp_err_t playPcm(const AudioPreview *audio, int64_t startedUs) {
    esp_err_t error = DSPetAudioDeviceOpen();
    if (error != ESP_OK) {
        return error;
    }
    size_t writtenSamples = 0;
    while (error == ESP_OK && writtenSamples < audio->samples) {
        size_t count = audio->samples - writtenSamples;
        if (count > kDSPetAudioDeviceBlockSamples) {
            count = kDSPetAudioDeviceBlockSamples;
        }
        error = DSPetAudioDeviceWrite(audio->pcm + writtenSamples, count, kWriteTimeoutMs);
        if (error == ESP_OK) {
            if (!writtenSamples) {
                printf("DSPET_DIRECT_FIRST_PCM round_us=%" PRId64 "\n", esp_timer_get_time() - startedUs);
            }
            writtenSamples += count;
        }
    }
    if (writtenSamples) {
        esp_err_t drained = DSPetAudioDeviceDrain(kDrainTimeoutMs);
        if (error == ESP_OK) {
            error = drained;
        }
    }
    esp_err_t closed = DSPetAudioDeviceClose();
    printf("DSPET_DIRECT_PLAY drained=%u samples=%zu total_us=%" PRId64 "\n",
           error == ESP_OK ? 1u : 0u,
           writtenSamples,
           esp_timer_get_time() - startedUs);
    return error == ESP_OK ? closed : error;
}

static esp_err_t playReply(const char *reply, int64_t startedUs) {
    AudioPreview audio = {
        .pcm = heap_caps_malloc(kReplySamples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
    };
    if (!audio.pcm) {
        return ESP_ERR_NO_MEM;
    }
    printHeap("before_tts");
    // 固定问题用例先收完整音频，再开 I²S，以单独验证网络与喇叭。
    esp_err_t error = DSPetDeepSeekSpeak(sSettings.key, reply, consumePcm, &audio, NULL);
    if (error == ESP_OK) {
        error = audio.converter.haveLowByte || !audio.samples ? ESP_ERR_INVALID_RESPONSE : playPcm(&audio, startedUs);
    }
    free(audio.pcm);
    return error;
}

esp_err_t DSPetDirectTestRun(void) {
    heap_caps_monitor_local_minimum_free_size_start();
    printHeap("before_wifi");
    esp_err_t  error  = DSPetWifiOpen();
    const bool opened = error == ESP_OK;
    if (error == ESP_OK) {
        error = DSPetWifiConnect(sSettings.ssid, sSettings.password);
    }
    mbedtls_platform_zeroize(sSettings.password, sizeof(sSettings.password));
    if (error == ESP_OK) {
        printHeap("wifi_connected");
        const struct timeval now = {.tv_sec = sSettings.unixTime};
        error                    = settimeofday(&now, NULL) == 0 ? ESP_OK : ESP_FAIL;
        printf("DSPET_DIRECT_CLOCK source=serial result=%s\n", esp_err_to_name(error));
    }

    const int64_t startedUs = esp_timer_get_time();
    char          reply[1024];
    if (error == ESP_OK) {
        const char *question =
            sSettings.shortReply ? "一加一等于几？只回答一个汉字。" : "海水为什么是咸的？用一句话回答。";
        error = DSPetDeepSeekReply(sSettings.key, question, NULL, reply, sizeof(reply), NULL);
    }
    if (error == ESP_OK) {
        printf("DSPET_DIRECT_REPLY %s\n", reply);
        error = playReply(reply, startedUs);
    }
    if (opened) {
        DSPetWifiClose();
    }
    DSPetDirectTestClear();
    printHeap("after_test");
    heap_caps_monitor_local_minimum_free_size_stop();
    return error;
}
