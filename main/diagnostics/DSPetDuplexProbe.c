#include "DSPetDuplexProbe.h"

#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>

#include "DSPetAudioDevice.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum {
    kBlockUs              = kDSPetAudioDeviceBlockSamples * 1000000 / kDSPetAudioDeviceSampleRateHz,
    kReportBlocks         = 250,
    kQuietBlocks          = 12,
    kRecordingBudgetBytes = 10 * kDSPetAudioDeviceSampleRateHz * sizeof(int16_t),
};

typedef struct {
    uint32_t index;
    int64_t  eofUs;
    unsigned blocks;
    unsigned dropped;
    unsigned maxIntervalUs;
    int      peak;
} ProbeStream;

static const char *const kTag = "duplex";

static int samplePeak(const DSPetAudioTapBlock *block) {
    int peak = 0;
    for (size_t i = 0; i < kDSPetAudioDeviceBlockSamples; ++i) {
        int magnitude = abs(block->samples[i]);
        if (magnitude > peak) {
            peak = magnitude;
        }
    }
    return peak;
}

static void updateStream(ProbeStream *stream, const DSPetAudioTapBlock *block, int peak) {
    if (stream->eofUs) {
        uint32_t step = block->blockIndex - stream->index;
        if (step > 1) {
            stream->dropped += step - 1;
        }
        unsigned intervalUs = (unsigned) (block->dmaEofUs - stream->eofUs);
        if (intervalUs > stream->maxIntervalUs) {
            stream->maxIntervalUs = intervalUs;
        }
    }
    stream->index = block->blockIndex;
    stream->eofUs = block->dmaEofUs;
    ++stream->blocks;
    if (peak > stream->peak) {
        stream->peak = peak;
    }
}

static void probeTask(void *context) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    ProbeStream rx           = {0};
    ProbeStream tx           = {0};
    int64_t     phaseMinUs   = INT64_MAX;
    int64_t     phaseMaxUs   = INT64_MIN;
    unsigned    activeBlocks = 0;
    unsigned    quietBlocks  = kQuietBlocks;
    for (;;) {
        DSPetAudioTapBlock block;
        if (!DSPetAudioDeviceTakeTap(&block, 1000)) {
            ESP_LOGW(kTag, "No DMA observations for 1000 ms");
            continue;
        }
        int peak = samplePeak(&block);
        if (block.source == kDSPetAudioTapPlayback) {
            updateStream(&tx, &block, peak);
            if (peak) {
                if (quietBlocks >= kQuietBlocks) {
                    ESP_LOGI(kTag, "TX reference active at block %" PRIu32, block.blockIndex);
                }
                quietBlocks = 0;
                ++activeBlocks;
            } else if (quietBlocks < kQuietBlocks && ++quietBlocks == kQuietBlocks) {
                ESP_LOGI(kTag, "TX reference silent for %u ms", kQuietBlocks * kBlockUs / 1000);
            }
            continue;
        }
        updateStream(&rx, &block, peak);
        if (tx.eofUs) {
            // TX 与 RX 的块序号各自起算。用 8 ms 周期估计最近块边界的相位差。
            int64_t phaseUs = (rx.eofUs - tx.eofUs) % kBlockUs;
            if (phaseUs > kBlockUs / 2) {
                phaseUs -= kBlockUs;
            } else if (phaseUs < -kBlockUs / 2) {
                phaseUs += kBlockUs;
            }
            if (phaseUs < phaseMinUs) {
                phaseMinUs = phaseUs;
            }
            if (phaseUs > phaseMaxUs) {
                phaseMaxUs = phaseUs;
            }
        }
        if (rx.blocks >= kReportBlocks && tx.blocks && phaseMinUs != INT64_MAX) {
            ESP_LOGI(kTag,
                     "RX/TX=%u/%u lost=%u/%u max_interval_us=%u/%u phase_us=%lld..%lld",
                     rx.blocks,
                     tx.blocks,
                     rx.dropped,
                     tx.dropped,
                     rx.maxIntervalUs,
                     tx.maxIntervalUs,
                     (long long) phaseMinUs,
                     (long long) phaseMaxUs);
            ESP_LOGI(kTag,
                     "TX_active=%u peak_mic/ref=%d/%d internal_min=%u B",
                     activeBlocks,
                     rx.peak,
                     tx.peak,
                     (unsigned) heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            rx.blocks = tx.blocks = 0;
            rx.dropped = tx.dropped = 0;
            rx.peak = tx.peak = 0;
            rx.maxIntervalUs = tx.maxIntervalUs = 0;
            phaseMinUs                          = INT64_MAX;
            phaseMaxUs                          = INT64_MIN;
            activeBlocks                        = 0;
        }
    }
}

esp_err_t DSPetDuplexProbeStart(void) {
    size_t     psramBytes  = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    uint8_t   *recording   = heap_caps_malloc(kRecordingBudgetBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const bool canAllocate = recording != NULL;
    free(recording);
    ESP_LOGI(kTag,
             "PSRAM=%u B, allocate %u B for 10 s recording: %s",
             (unsigned) psramBytes,
             (unsigned) kRecordingBudgetBytes,
             canAllocate ? "ok" : "unavailable");

    TaskHandle_t task;
    if (xTaskCreate(probeTask, "duplex-probe", 3072, NULL, 5, &task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t error = DSPetAudioDeviceStartTap();
    if (error != ESP_OK) {
        vTaskDelete(task);
        return error;
    }
    xTaskNotifyGive(task);
    return ESP_OK;
}
