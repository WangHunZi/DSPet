#include "DSPetAecTest.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "DSPetAec.h"
#include "DSPetAecSync.h"
#include "DSPetAudioDevice.h"
#include "DSPetPlayback.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

extern const int16_t dspetPlaybackDemoStart[];
extern const int16_t dspetPlaybackDemoEnd[];

enum {
    kPlaybackToMicUs = 2100,
    kFilterLeadUs    = 1000,
    kMicWarmupUs     = 350000,
    kQuietMeasureUs  = 1550000,
    kQuietMaxFrames  = 64,
    kAdaptationUs    = 2000000,
    kMinMeasureUs    = 5000000,
    kTapWaitMs       = 25,
    kMeterPeak       = 4096,
};

typedef struct {
    uint64_t squares;
    size_t   count;
} Power;

typedef struct {
    DSPetAecSync sync;
    DSPetAec    *aec;
    int16_t     *mic;
    int16_t     *reference;
    int16_t     *output;
    size_t       frameSamples;
    unsigned     rxBlocks;
    unsigned     txBlocks;
    unsigned     lostBlocks;
    unsigned     frames;
    int64_t      maxProcessUs;
    size_t       initialInternalBytes;
    size_t       minInternalBytes;
    int64_t      captureStartUs;
    int64_t      playbackStartUs;
    Power        quietMic;
    uint64_t     quietFrameSquares[kQuietMaxFrames];
    size_t       quietFrames;
    Power        rawMic;
    Power        residual;
    Power        playedReference;
} AecTest;

static void measurePower(Power *power, const int16_t *samples, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        int32_t magnitude = abs(samples[i]);
        int32_t metered   = magnitude < kMeterPeak ? magnitude : kMeterPeak;
        power->squares += (uint32_t) (metered * metered);
    }
    power->count += count;
}

static double rms(Power power) { return power.count ? sqrt((double) power.squares / power.count) : 0; }

static void measureHeap(AecTest *test) {
    size_t freeBytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (freeBytes < test->minInternalBytes) {
        test->minInternalBytes = freeBytes;
    }
}

static esp_err_t collectFrame(AecTest *test) {
    DSPetAudioTapBlock block;
    if (!DSPetAudioDeviceTakeTap(&block, kTapWaitMs)) {
        return ESP_OK;
    }

    const bool         isMic  = block.source == kDSPetAudioTapMic;
    DSPetAecSyncSource source = isMic ? kDSPetAecSyncMic : kDSPetAecSyncReference;
    if (!DSPetAecSyncPush(
            &test->sync, source, block.samples, kDSPetAudioDeviceBlockSamples, block.blockIndex, block.dmaEofUs)) {
        ++test->lostBlocks;
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (isMic) {
        ++test->rxBlocks;
    } else {
        ++test->txBlocks;
    }

    DSPetAecSyncResult syncResult = DSPetAecSyncTake(&test->sync, test->mic, test->reference, test->frameSamples);
    if (syncResult == kDSPetAecSyncWaiting) {
        return ESP_OK;
    }
    if (syncResult == kDSPetAecSyncLost) {
        ++test->lostBlocks;
        return ESP_ERR_INVALID_RESPONSE;
    }

    int64_t   startUs = esp_timer_get_time();
    esp_err_t error   = DSPetAecProcess(test->aec, test->mic, test->reference, test->output, test->frameSamples);
    if (error != ESP_OK) {
        return error;
    }
    int64_t processUs = esp_timer_get_time() - startUs;
    if (processUs > test->maxProcessUs) {
        test->maxProcessUs = processUs;
    }
    ++test->frames;
    measureHeap(test);

    if (!test->playbackStartUs && startUs - test->captureStartUs >= kMicWarmupUs) {
        Power framePower = {0};
        measurePower(&framePower, test->mic, test->frameSamples);
        test->quietMic.squares += framePower.squares;
        test->quietMic.count += framePower.count;
        if (test->quietFrames < kQuietMaxFrames) {
            test->quietFrameSquares[test->quietFrames++] = framePower.squares;
        }
    } else if (test->playbackStartUs && startUs - test->playbackStartUs >= kAdaptationUs) {
        // 只比较自适应稳定后的长窗口，减少回声消除帧延迟在窗口两端的影响。
        measurePower(&test->rawMic, test->mic, test->frameSamples);
        measurePower(&test->residual, test->output, test->frameSamples);
        measurePower(&test->playedReference, test->reference, test->frameSamples);
    }
    return ESP_OK;
}

static esp_err_t runPlayback(AecTest *test, size_t sampleCount, bool *playbackIdle) {
    esp_err_t result = DSPetPlaybackStart(dspetPlaybackDemoStart, sampleCount);
    if (result != ESP_OK) {
        return result;
    }
    *playbackIdle         = false;
    test->playbackStartUs = esp_timer_get_time();
    const int64_t deadlineUs =
        test->playbackStartUs + (int64_t) sampleCount * 1000000 / kDSPetAudioDeviceSampleRateHz + 2000000;
    int64_t cancelledUs = 0;
    for (;;) {
        DSPetPlaybackEvent event;
        while (DSPetPlaybackTakeEvent(&event)) {
            if (event.kind == kDSPetPlaybackEventQueued) {
                continue;
            }
            *playbackIdle = true;
            if (result != ESP_OK) {
                return result;
            }
            if (event.kind == kDSPetPlaybackEventFailed) {
                return event.error;
            }
            return event.kind == kDSPetPlaybackEventFinished ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
        }
        if (result == ESP_OK) {
            result = collectFrame(test);
        } else {
            DSPetAudioTapBlock ignored;
            DSPetAudioDeviceTakeTap(&ignored, kTapWaitMs);
        }
        const int64_t nowUs = esp_timer_get_time();
        if (result == ESP_OK && nowUs >= deadlineUs) {
            result = ESP_ERR_TIMEOUT;
        }
        if (result != ESP_OK && !cancelledUs) {
            DSPetPlaybackCancel();
            cancelledUs = nowUs;
        }
        if (cancelledUs && nowUs - cancelledUs > 1000000) {
            puts("DSPET_TEST_METRIC playback_task_unresponsive=1 recovery=reboot");
            return ESP_ERR_TIMEOUT;
        }
    }
}

static int compareFrameSquares(const void *left, const void *right) {
    uint64_t a = *(const uint64_t *) left;
    uint64_t b = *(const uint64_t *) right;
    return (a > b) - (a < b);
}

static esp_err_t evaluate(AecTest *test) {
    const double quiet = rms(test->quietMic);
    // 保留整段底噪供排查，用帧功率中位数避免短促尖峰主导比较。
    qsort(test->quietFrameSquares, test->quietFrames, sizeof(test->quietFrameSquares[0]), compareFrameSquares);
    const double  typicalQuiet  = sqrt((double) test->quietFrameSquares[test->quietFrames / 2] / test->frameSamples);
    const double  raw           = rms(test->rawMic);
    const double  residual      = rms(test->residual);
    const double  reference     = rms(test->playedReference);
    const double  reductionDb   = raw > 0 ? 20 * log10(raw / fmax(residual, 1.0)) : 0;
    const int64_t frameBudgetUs = (int64_t) test->frameSamples * 1000000 / kDSPetAudioDeviceSampleRateHz;
    printf(
        "DSPET_TEST_METRIC aec_quiet_rms=%.1f aec_quiet_median_rms=%.1f aec_raw_rms=%.1f aec_out_rms=%.1f ref_rms=%.1f"
        " reduction_db=%.2f measured_samples=%u\n",
        quiet,
        typicalQuiet,
        raw,
        residual,
        reference,
        reductionDb,
        (unsigned) test->rawMic.count);
    printf("DSPET_TEST_METRIC aec_frames=%u aec_frame_samples=%u max_process_us=%" PRId64 " frame_budget_us=%" PRId64
           " ref_offset_us=%u\n",
           test->frames,
           (unsigned) test->frameSamples,
           test->maxProcessUs,
           frameBudgetUs,
           kPlaybackToMicUs - kFilterLeadUs);

    if (!test->quietFrames || test->rawMic.count < kMinMeasureUs / 1000 * (kDSPetAudioDeviceSampleRateHz / 1000) ||
        reference < 100 || raw < 30 || raw < 1.5 * typicalQuiet) {
        puts("DSPET_TEST_METRIC aec_reason=insufficient_echo_or_noisy_room");
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (reductionDb < 6 || test->maxProcessUs >= frameBudgetUs) {
        puts("DSPET_TEST_METRIC aec_reason=threshold_not_met");
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t DSPetAecTestRun(void) {
    AecTest *test = heap_caps_calloc(1, sizeof(*test), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!test) {
        return ESP_ERR_NO_MEM;
    }
    test->initialInternalBytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    test->minInternalBytes     = test->initialInternalBytes;
    esp_err_t result           = DSPetAecCreate(&test->aec);
    int16_t  *frames           = NULL;
    if (result == ESP_OK) {
        test->frameSamples = DSPetAecFrameSamples(test->aec);
        frames = heap_caps_malloc(3 * test->frameSamples * sizeof(*frames), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!frames) {
            result = ESP_ERR_NO_MEM;
        }
    }

    bool deviceOpen   = false;
    bool playbackIdle = true;
    if (result == ESP_OK) {
        test->mic       = frames;
        test->reference = frames + test->frameSamples;
        test->output    = frames + 2 * test->frameSamples;
        DSPetAecSyncInit(&test->sync, kPlaybackToMicUs - kFilterLeadUs);
        result     = DSPetAudioDeviceOpen();
        deviceOpen = result == ESP_OK;
    }
    if (deviceOpen) {
        result = DSPetAudioDeviceStartTap();
        measureHeap(test);
    }
    if (result == ESP_OK) {
        puts("AEC 测试将播放一段固定语音，请保持安静。");
        test->captureStartUs = esp_timer_get_time();
        while (result == ESP_OK && esp_timer_get_time() - test->captureStartUs < kQuietMeasureUs) {
            result = collectFrame(test);
        }
    }
    if (result == ESP_OK) {
        size_t sampleCount = ((uintptr_t) dspetPlaybackDemoEnd - (uintptr_t) dspetPlaybackDemoStart) / sizeof(int16_t);
        result             = runPlayback(test, sampleCount, &playbackIdle);
    }
    if (deviceOpen && playbackIdle) {
        esp_err_t error = DSPetAudioDeviceClose();
        if (result == ESP_OK) {
            result = error;
        }
    }
    printf("DSPET_TEST_METRIC aec_rx_blocks=%u aec_tx_blocks=%u aec_lost=%u internal_before=%u internal_min=%u\n",
           test->rxBlocks,
           test->txBlocks,
           test->lostBlocks,
           (unsigned) test->initialInternalBytes,
           (unsigned) test->minInternalBytes);
    if (result == ESP_OK) {
        result = evaluate(test);
    }
    if (result != ESP_OK) {
        printf("DSPET_TEST_METRIC aec_error=%s\n", esp_err_to_name(result));
    }
    free(frames);
    DSPetAecDestroy(test->aec);
    free(test);
    return result;
}
