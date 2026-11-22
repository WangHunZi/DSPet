#include "DSPetAlignmentTest.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "DSPetAudioCorrelation.h"
#include "DSPetAudioDevice.h"
#include "DSPetPlayback.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

enum {
    kTrials          = 3,
    kSweepSamples    = 2048,
    kSweepPeak       = 1536,
    kCaptureSamples  = 48 * kDSPetAudioDeviceBlockSamples,
    kMicClipPeak     = 4096,
    kTapWaitMs       = 25,
    kEarliestDelayUs = -8000,
    kLatestDelayUs   = 100000,
    kMaxSpreadUs     = 4000,
};

typedef struct {
    int16_t  samples[kCaptureSamples];
    size_t   count;
    uint32_t lastBlock;
    int64_t  firstEofUs;
    bool     lost;
} Stream;

typedef struct {
    Stream mic;
    Stream tx;
} Trial;

typedef struct {
    int16_t sweep[kSweepSamples];
    Trial   trials[kTrials];
} Capture;

static void prepareSweep(int16_t *sweep) {
    const float pi       = 3.14159265359f;
    const float duration = (float) kSweepSamples / kDSPetAudioDeviceSampleRateHz;
    for (size_t i = 0; i < kSweepSamples; ++i) {
        float time     = (float) i / kDSPetAudioDeviceSampleRateHz;
        float phase    = 2 * pi * (450 * time + 0.5f * (3200 - 450) * time * time / duration);
        float envelope = sinf(pi * i / (kSweepSamples - 1));
        sweep[i]       = (int16_t) (kSweepPeak * envelope * envelope * sinf(phase));
    }
}

static void storeBlock(Trial *trial, const DSPetAudioTapBlock *block) {
    Stream *stream = block->source == kDSPetAudioTapMic ? &trial->mic : &trial->tx;
    if (stream->count == kCaptureSamples) {
        return;
    }
    if (stream->count == 0) {
        stream->firstEofUs = block->dmaEofUs;
    } else if (block->blockIndex - stream->lastBlock != 1) {
        stream->lost = true;
    }
    stream->lastBlock = block->blockIndex;
    int16_t *samples  = stream->samples + stream->count;
    if (stream == &trial->mic) {
        // 抑制偶发尖峰对相关系数的影响。正式录音和播放参考保持原样。
        for (size_t i = 0; i < kDSPetAudioDeviceBlockSamples; ++i) {
            int16_t value = block->samples[i];
            samples[i]    = value > kMicClipPeak ? kMicClipPeak : (value < -kMicClipPeak ? -kMicClipPeak : value);
        }
    } else {
        memcpy(samples, block->samples, sizeof(block->samples));
    }
    stream->count += kDSPetAudioDeviceBlockSamples;
}

static esp_err_t collectTrial(const int16_t *sweep, Trial *trial, bool *playbackIdle) {
    esp_err_t error = DSPetPlaybackStart(sweep, kSweepSamples);
    if (error != ESP_OK) {
        return error;
    }
    *playbackIdle            = false;
    const int64_t deadlineUs = esp_timer_get_time() + 1000000;
    bool          cancelled  = false;
    for (;;) {
        DSPetPlaybackEvent event;
        while (DSPetPlaybackTakeEvent(&event)) {
            if (event.kind == kDSPetPlaybackEventQueued) {
                continue;
            }
            *playbackIdle = true;
            if (event.kind == kDSPetPlaybackEventFailed) {
                return event.error;
            }
            if (event.kind != kDSPetPlaybackEventFinished || cancelled) {
                return ESP_ERR_TIMEOUT;
            }
        }
        if (*playbackIdle && trial->mic.count == kCaptureSamples && trial->tx.count == kCaptureSamples) {
            return (trial->mic.lost || trial->tx.lost) ? ESP_ERR_INVALID_RESPONSE : ESP_OK;
        }
        const int64_t nowUs = esp_timer_get_time();
        if (nowUs >= deadlineUs) {
            if (*playbackIdle || nowUs >= deadlineUs + 1000000) {
                return ESP_ERR_TIMEOUT;
            }
            if (!cancelled) {
                DSPetPlaybackCancel();
                cancelled = true;
            }
        }
        DSPetAudioTapBlock block;
        if (DSPetAudioDeviceTakeTap(&block, kTapWaitMs)) {
            storeBlock(trial, &block);
        }
    }
}

static int64_t sampleTimeUs(const Stream *stream, size_t index) {
    // 两路都从各自首个 DMA EOF 起算，比较时共同的块长抵消。
    return stream->firstEofUs + (int64_t) index * 1000000 / kDSPetAudioDeviceSampleRateHz;
}

static esp_err_t measureTrial(const Trial *trial, unsigned number, int64_t *delayUs) {
    size_t txFirst = 0;
    size_t txEnd   = trial->tx.count;
    while (txFirst < txEnd && !trial->tx.samples[txFirst]) {
        ++txFirst;
    }
    while (txEnd > txFirst && !trial->tx.samples[txEnd - 1]) {
        --txEnd;
    }
    const size_t referenceCount = txEnd - txFirst;
    if (referenceCount < kSweepSamples * 3 / 4 || referenceCount > trial->mic.count) {
        puts("DSPET_TEST_METRIC alignment_reason=incomplete_tx_reference");
        return ESP_ERR_INVALID_RESPONSE;
    }
    const int64_t txUs        = sampleTimeUs(&trial->tx, txFirst);
    size_t        firstOffset = 0;
    size_t        lastOffset  = trial->mic.count - referenceCount;
    while (firstOffset < lastOffset && sampleTimeUs(&trial->mic, firstOffset) - txUs < kEarliestDelayUs) {
        ++firstOffset;
    }
    while (lastOffset > firstOffset && sampleTimeUs(&trial->mic, lastOffset) - txUs > kLatestDelayUs) {
        --lastOffset;
    }
    DSPetAudioCorrelation match;
    if (firstOffset >= lastOffset || !DSPetAudioCorrelationFind(trial->tx.samples + txFirst,
                                                                referenceCount,
                                                                trial->mic.samples,
                                                                trial->mic.count,
                                                                firstOffset,
                                                                lastOffset,
                                                                &match)) {
        puts("DSPET_TEST_METRIC alignment_reason=no_acoustic_match");
        return ESP_ERR_INVALID_RESPONSE;
    }
    *delayUs = sampleTimeUs(&trial->mic, match.offset) - txUs;
    printf("DSPET_TEST_METRIC alignment_trial=%u delay_us=%" PRId64 " correlation_milli=%u rx=%u tx=%u\n",
           number,
           *delayUs,
           (unsigned) (match.score * 1000 + 0.5f),
           (unsigned) trial->mic.count,
           (unsigned) trial->tx.count);
    return (match.score >= 0.20f && match.offset != firstOffset && match.offset != lastOffset)
               ? ESP_OK
               : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t DSPetAlignmentTestRun(void) {
    Capture *capture = heap_caps_calloc(1, sizeof(*capture), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!capture) {
        return ESP_ERR_NO_MEM;
    }
    prepareSweep(capture->sweep);
    esp_err_t result = DSPetAudioDeviceOpen();
    if (result != ESP_OK) {
        free(capture);
        return result;
    }
    result            = DSPetAudioDeviceStartTap();
    bool playbackIdle = true;
    if (result == ESP_OK) {
        // 麦克风在上电后需要一定数量的 SCK 周期，热身时消费 DMA 块。
        const int64_t readyUs = esp_timer_get_time() + 400000;
        while (esp_timer_get_time() < readyUs) {
            DSPetAudioTapBlock ignored;
            DSPetAudioDeviceTakeTap(&ignored, kTapWaitMs);
        }
        for (size_t i = 0; i < kTrials && result == ESP_OK; ++i) {
            result = collectTrial(capture->sweep, &capture->trials[i], &playbackIdle);
        }
    }
    if (!playbackIdle) {
        // 任务仍可能引用 sweep，保留缓冲区和设备供复位回收。
        puts("DSPET_TEST_METRIC playback_task_unresponsive=1 recovery=reboot");
        return result;
    }
    esp_err_t closeResult = DSPetAudioDeviceClose();
    if (result == ESP_OK) {
        result = closeResult;
    }
    if (result == ESP_OK) {
        int64_t minUs = INT64_MAX;
        int64_t maxUs = INT64_MIN;
        for (unsigned i = 0; i < kTrials; ++i) {
            int64_t delayUs;
            result = measureTrial(&capture->trials[i], i + 1, &delayUs);
            if (result != ESP_OK) {
                break;
            }
            if (delayUs < minUs) {
                minUs = delayUs;
            }
            if (delayUs > maxUs) {
                maxUs = delayUs;
            }
        }
        if (result == ESP_OK) {
            printf("DSPET_TEST_METRIC alignment_min_us=%" PRId64 " alignment_max_us=%" PRId64
                   " alignment_spread_us=%" PRId64 "\n",
                   minUs,
                   maxUs,
                   maxUs - minUs);
            if (maxUs - minUs > kMaxSpreadUs) {
                result = ESP_ERR_INVALID_RESPONSE;
            }
        }
    }
    free(capture);
    return result;
}
