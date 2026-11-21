#include "pet_animation.h"
#include "asset_config.h"
#include "display.h"
#include "DSPetButton.h"
#include "indexed_frame.h"
#include "action_state.h"
#include "sdkconfig.h"
#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "miniz.h"

void pet_play(void)
{
    static const char *TAG = "pet";
    const pet_clip_t *clip = &pet_clips[0];
    size_t size = (uintptr_t)clip->end - (uintptr_t)clip->start;
    for (size_t i = 0; i < PET_CLIP_COUNT; ++i) {
        size_t bytes = (uintptr_t)pet_clips[i].end - (uintptr_t)pet_clips[i].start;
        assert(bytes == pet_clips[i].offsets[pet_clips[i].frame_count]);
    }
    uint8_t *decoded = heap_caps_malloc(DISPLAY_FRAME_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint8_t *previous = heap_caps_malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    // ROM convenience inflater allocates this large state on the stack;
    // keep it on the heap to fit app_main's normal task stack.
    tinfl_decompressor *decoder = calloc(1, sizeof(*decoder));
    assert(decoded && decoder && previous);
    ESP_LOGI(TAG, "%s: %u frames, %u fps, %u compressed bytes",
             clip->name, clip->frame_count, clip->fps, (unsigned)size);
    ESP_LOGI(TAG, "Free internal heap after decoder init: %u bytes",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    ESP_LOGI(TAG, "BOOT switches through %u clips", (unsigned)PET_CLIP_COUNT);
    int64_t epoch = esp_timer_get_time();
    action_state_t action = {.started_us = epoch};
    uint32_t presses = 0;
#ifdef CONFIG_DSPET_AUTO_PLAY
    const bool automatic = true;
#else
    const bool automatic = false;
#endif
    int64_t last_log = epoch;
    unsigned sent = 0;
    int64_t decode_total = 0, transfer_total = 0;
    size_t last_decoded = SIZE_MAX;
    for (;;) {
        DSPetButtonTakeCount(&presses);
        int64_t now_us = esp_timer_get_time();
        int64_t duration_us = (int64_t)clip->frame_count * 1000000 / clip->fps;
        if (action_state_update(&action, PET_CLIP_COUNT, presses, now_us, duration_us, automatic)) {
            clip = &pet_clips[action.index];
            size = (uintptr_t)clip->end - (uintptr_t)clip->start;
            epoch = action.started_us;
            last_log = epoch;
            sent = 0;
            decode_total = transfer_total = 0;
            last_decoded = SIZE_MAX;
            ESP_LOGI(TAG, "Action: %s (BOOT presses=%" PRIu32 ")", clip->name, presses);
        }
        int64_t elapsed = esp_timer_get_time() - epoch;
        uint64_t sequence = (uint64_t)elapsed * clip->fps / 1000000;
        size_t index = sequence % clip->frame_count;
        int64_t decode_start = esp_timer_get_time();
        if (last_decoded != index) {
            size_t first = index - index % clip->keyframe_interval;
            if (last_decoded < index && last_decoded >= first) first = last_decoded + 1;
            // Catch up skipped deltas, or seek from the nearest keyframe.
            // Never XOR a delta with an unrelated frame from the prior clip.
            for (size_t frame = first; frame <= index; ++frame) {
                uint32_t start = clip->offsets[frame], end = clip->offsets[frame + 1];
                assert(start < end && end <= size);
                size_t input_size = end - start, output_size = DISPLAY_FRAME_BYTES;
                tinfl_init(decoder);
                tinfl_status status = tinfl_decompress(decoder, clip->start + start,
                    &input_size, decoded, decoded, &output_size,
                    TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
                if (status != TINFL_STATUS_DONE || output_size != 512 + DISPLAY_WIDTH * DISPLAY_HEIGHT || input_size != end - start) {
                    ESP_LOGE(TAG, "Invalid compressed frame %u", (unsigned)frame);
                    abort();
                }
                indexed_frame_restore_delta(decoded, previous, DISPLAY_WIDTH * DISPLAY_HEIGHT,
                                             frame % clip->keyframe_interval == 0);
            }
            if (!indexed_frame_expand(decoded, DISPLAY_WIDTH * DISPLAY_HEIGHT, DISPLAY_FRAME_BYTES)) {
                ESP_LOGE(TAG, "Indexed frame buffer is too small");
                abort();
            }
            last_decoded = index;
        }
        int64_t transfer_start = esp_timer_get_time();
        decode_total += transfer_start - decode_start;
        ESP_ERROR_CHECK(display_present(decoded, DISPLAY_FRAME_BYTES));
        transfer_total += esp_timer_get_time() - transfer_start;
        ++sent;

        // Use wall time, so a slower SPI configuration drops stale frames
        // instead of making a ten-second motion take a minute to play.
        int64_t deadline = epoch + (int64_t)((sequence + 1) * 1000000 / clip->fps);
        int64_t now = esp_timer_get_time();
        if (now - last_log >= 3000000) {
            ESP_LOGI(TAG, "%s frame=%u/%u fps=%.1f decode=%.2fms SPI=%.2fms",
                     clip->name, (unsigned)index, clip->frame_count,
                     (sent - 1) * 1000000.0 / (now - epoch),
                     decode_total / (sent * 1000.0), transfer_total / (sent * 1000.0));
            last_log = now;
        }
        int64_t remaining_us = deadline - now;
        // Round up so waking just before a frame boundary does not resend
        // the previous frame and consume another full SPI transfer.
        TickType_t delay = remaining_us > 0
            ? pdMS_TO_TICKS((remaining_us + 999) / 1000) : 1;
        vTaskDelay(delay > 0 ? delay : 1);
    }
}
