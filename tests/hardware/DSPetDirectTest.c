#include "DSPetDirectTest.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "DSPetAudioDevice.h"
#include "DSPetDeepSeek.h"
#include "DSPetPcm24kTo16k.h"
#include "DSPetWifi.h"
#include "esp_heap_caps.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "mbedtls/platform_util.h"

enum { kWriteTimeoutMs = 40, kDrainTimeoutMs = 120, kTimeSyncMs = 10000, kSpeakerAttenuation = 10 };

static struct {
    char ssid[kDSPetWifiSsidBytes + 1];
    char password[kDSPetWifiPasswordBytes + 1];
    char key[kDSPetDeepSeekKeyBytes + 1];
} sSettings;

esp_err_t DSPetDirectTestPrepare(const char *ssid, const char *password, const char *key) {
    const size_t ssidBytes     = strlen(ssid);
    const size_t passwordBytes = password ? strlen(password) : 0;
    const size_t keyBytes      = key ? strlen(key) : 0;
    if (!ssidBytes || ssidBytes > kDSPetWifiSsidBytes ||
        (password && (passwordBytes < 8 || passwordBytes > kDSPetWifiPasswordBytes)) ||
        (key && (!keyBytes || keyBytes > kDSPetDeepSeekKeyBytes))) {
        return ESP_ERR_INVALID_ARG;
    }
    DSPetDirectTestClear();
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
    bool             haveLowByte;
    uint8_t          lowByte;
    int16_t          output[kDSPetAudioDeviceBlockSamples];
    size_t           used;
    size_t           writtenSamples;
    int64_t          roundStartedUs;
} AudioOutput;

static esp_err_t playBlock(AudioOutput *output) {
    esp_err_t error = DSPetAudioDeviceWrite(output->output, output->used, kWriteTimeoutMs);
    if (error == ESP_OK) {
        if (!output->writtenSamples) {
            printf("DSPET_DIRECT_FIRST_PCM round_us=%" PRId64 "\n", esp_timer_get_time() - output->roundStartedUs);
        }
        output->writtenSamples += output->used;
        output->used = 0;
    }
    return error;
}

static esp_err_t consumePcm(const uint8_t *bytes, size_t count, void *context) {
    AudioOutput *output = context;
    for (size_t i = 0; i < count; ++i) {
        if (!output->haveLowByte) {
            output->lowByte     = bytes[i];
            output->haveLowByte = true;
            continue;
        }
        const int16_t sample = (int16_t) ((uint16_t) bytes[i] << 8 | output->lowByte);
        output->haveLowByte  = false;
        int16_t      pair[2];
        const size_t available = DSPetPcm24kTo16kPush(&output->converter, sample, pair);
        for (size_t index = 0; index < available; ++index) {
            output->output[output->used++] = pair[index] / kSpeakerAttenuation;
        }
        if (output->used == kDSPetAudioDeviceBlockSamples) {
            esp_err_t error = playBlock(output);
            if (error != ESP_OK) {
                return error;
            }
        }
    }
    return ESP_OK;
}

static esp_err_t playReply(const char *reply, int64_t startedUs) {
    AudioOutput output = {.roundStartedUs = startedUs};
    esp_err_t   error  = DSPetAudioDeviceOpen();
    if (error != ESP_OK) {
        return error;
    }
    printHeap("before_tts");
    error = DSPetDeepSeekSpeak(sSettings.key, reply, consumePcm, &output);
    if (error == ESP_OK && (output.haveLowByte || (!output.writtenSamples && !output.used))) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error == ESP_OK && output.used) {
        error = playBlock(&output);
    }
    if (output.writtenSamples) {
        esp_err_t drained = DSPetAudioDeviceDrain(kDrainTimeoutMs);
        if (error == ESP_OK) {
            error = drained;
        }
    }
    esp_err_t closed = DSPetAudioDeviceClose();
    printf("DSPET_DIRECT_PLAY drained=%u samples=%zu total_us=%" PRId64 "\n",
           error == ESP_OK ? 1u : 0u,
           output.writtenSamples,
           esp_timer_get_time() - startedUs);
    return error == ESP_OK ? closed : error;
}

static esp_err_t syncClock(void) {
    const esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
    esp_err_t               error  = esp_netif_sntp_init(&config);
    if (error == ESP_OK) {
        error = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(kTimeSyncMs));
        esp_netif_sntp_deinit();
    }
    printf("DSPET_DIRECT_CLOCK result=%s\n", esp_err_to_name(error));
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
        error = syncClock();
    }

    const int64_t startedUs = esp_timer_get_time();
    char          reply[1024];
    if (error == ESP_OK) {
        error = DSPetDeepSeekReply(sSettings.key, "海水为什么是咸的？用一句话回答。", reply, sizeof(reply));
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
