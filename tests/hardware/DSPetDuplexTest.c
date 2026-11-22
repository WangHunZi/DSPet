#include "DSPetDuplexTest.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "DSPetAudioDevice.h"
#include "DSPetPlayback.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

extern const int16_t dspetPlaybackDemoStart[];
extern const int16_t dspetPlaybackDemoEnd[];

enum {
    kTapWaitMs      = 25,
    kQuietBlocks    = 8,
    kRecordingBytes = 10 * kDSPetAudioDeviceSampleRateHz * sizeof(int16_t),
};

typedef struct {
    uint32_t index;
    unsigned count;
    unsigned lost;
} SampleStream;

typedef struct {
    SampleStream mic;
    SampleStream playback;
    unsigned     activeBlocks;
    unsigned     quietBlocks;
} Capture;

static void collectBlock(Capture *capture) {
    DSPetAudioTapBlock block;
    if (!DSPetAudioDeviceTakeTap(&block, kTapWaitMs)) {
        return;
    }
    SampleStream *stream = block.source == kDSPetAudioTapMic ? &capture->mic : &capture->playback;
    if (stream->count) {
        stream->lost += block.blockIndex - stream->index - 1;
    }
    stream->index = block.blockIndex;
    ++stream->count;
    if (block.source != kDSPetAudioTapPlayback) {
        return;
    }
    for (size_t i = 0; i < kDSPetAudioDeviceBlockSamples; ++i) {
        if (block.samples[i]) {
            ++capture->activeBlocks;
            capture->quietBlocks = 0;
            return;
        }
    }
    ++capture->quietBlocks;
}

static esp_err_t waitForQuiet(Capture *capture) {
    capture->quietBlocks = 0;
    int64_t deadlineUs   = esp_timer_get_time() + 300000;
    while (capture->quietBlocks < kQuietBlocks && esp_timer_get_time() < deadlineUs) {
        collectBlock(capture);
    }
    return capture->quietBlocks >= kQuietBlocks ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t runClip(Capture *capture, size_t sampleCount, bool cancel, bool *playbackIdle) {
    const unsigned activeBefore = capture->activeBlocks;
    esp_err_t      error        = DSPetPlaybackStart(dspetPlaybackDemoStart, sampleCount);
    if (error != ESP_OK) {
        return error;
    }
    *playbackIdle                   = false;
    int64_t       nowUs             = esp_timer_get_time();
    const int64_t cancelAtUs        = nowUs + 1500000;
    const int64_t deadlineUs        = nowUs + (int64_t) sampleCount * 1000000 / kDSPetAudioDeviceSampleRateHz + 2000000;
    int64_t       cancelRequestedUs = 0;
    bool          timedOut          = false;
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
            const DSPetPlaybackEventKind expected = cancel ? kDSPetPlaybackEventCancelled : kDSPetPlaybackEventFinished;
            if (timedOut || event.kind != expected || capture->activeBlocks == activeBefore) {
                return ESP_ERR_INVALID_RESPONSE;
            }
            if (cancel) {
                int64_t drainUs = event.occurredUs - cancelRequestedUs;
                printf("DSPET_TEST_METRIC cancel_drain_us=%" PRId64 "\n", drainUs);
            }
            return waitForQuiet(capture);
        }

        nowUs = esp_timer_get_time();
        if (!cancelRequestedUs && ((cancel && nowUs >= cancelAtUs) || nowUs >= deadlineUs)) {
            timedOut          = nowUs >= deadlineUs;
            cancelRequestedUs = nowUs;
            DSPetPlaybackCancel();
            // 等到终态再归还设备，避免播放任务仍持有 I²S。
        }
        if (cancelRequestedUs && nowUs - cancelRequestedUs > 1000000) {
            puts("DSPET_TEST_METRIC playback_task_unresponsive=1 recovery=reboot");
            return ESP_ERR_TIMEOUT;
        }
        collectBlock(capture);
    }
}

esp_err_t DSPetDuplexTestRun(void) {
    void *recording = heap_caps_malloc(kRecordingBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!recording) {
        puts("DSPET_TEST_METRIC psram_recording_buffer=unavailable");
        return ESP_ERR_NO_MEM;
    }
    free(recording);

    esp_err_t result = DSPetAudioDeviceOpen();
    if (result != ESP_OK) {
        return result;
    }
    result                    = DSPetAudioDeviceStartTap();
    bool         playbackIdle = true;
    Capture      capture      = {0};
    const size_t sampleCount =
        ((uintptr_t) dspetPlaybackDemoEnd - (uintptr_t) dspetPlaybackDemoStart) / sizeof(int16_t);
    if (result == ESP_OK) {
        result = waitForQuiet(&capture);
    }
    if (result == ESP_OK) {
        result = runClip(&capture, sampleCount, true, &playbackIdle);
    }
    if (result == ESP_OK) {
        result = runClip(&capture, sampleCount, false, &playbackIdle);
    }
    if (playbackIdle) {
        esp_err_t closeResult = DSPetAudioDeviceClose();
        if (result == ESP_OK) {
            result = closeResult;
        }
    }
    printf("DSPET_TEST_METRIC rx=%u tx=%u lost_rx=%u lost_tx=%u active_tx=%u psram_bytes=%u\n",
           capture.mic.count,
           capture.playback.count,
           capture.mic.lost,
           capture.playback.lost,
           capture.activeBlocks,
           (unsigned) kRecordingBytes);
    if (result == ESP_OK) {
        unsigned rx         = capture.mic.count;
        unsigned tx         = capture.playback.count;
        unsigned difference = rx > tx ? rx - tx : tx - rx;
        if (rx < kQuietBlocks || difference > 2 || capture.mic.lost || capture.playback.lost) {
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    return result;
}
