#include "pet_animation.h"
#include "asset_config.h"
#include "display.h"
#include "button.h"
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
    // ROM convenience inflater allocates this large state on the stack;
    // keep it on the heap to fit app_main's normal task stack.
    tinfl_decompressor *decoder = calloc(1, sizeof(*decoder));
    assert(decoded && decoder);
    ESP_LOGI(TAG, "%s: %u frames, %u fps, %u compressed bytes",
             clip->name, clip->frame_count, clip->fps, (unsigned)size);
    ESP_LOGI(TAG, "Free internal heap after decoder init: %u bytes",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    ESP_LOGI(TAG, "BOOT switches through %u clips", (unsigned)PET_CLIP_COUNT);
    int64_t epoch = esp_timer_get_time();
    int64_t last_log = epoch;
    unsigned sent = 0;
    for (;;) {
        uint32_t presses;
        if (button_take_count(&presses)) {
            clip = &pet_clips[presses % PET_CLIP_COUNT];
            size = (uintptr_t)clip->end - (uintptr_t)clip->start;
            epoch = esp_timer_get_time();
            last_log = epoch;
            sent = 0;
            ESP_LOGI(TAG, "Action: %s (BOOT presses=%" PRIu32 ")", clip->name, presses);
        }
        int64_t elapsed = esp_timer_get_time() - epoch;
        uint64_t sequence = (uint64_t)elapsed * clip->fps / 1000000;
        size_t index = sequence % clip->frame_count;
        uint32_t start = clip->offsets[index], end = clip->offsets[index + 1];
        assert(start < end && end <= size);
        size_t input_size = end - start, output_size = DISPLAY_FRAME_BYTES;
        tinfl_init(decoder);
        tinfl_status status = tinfl_decompress(decoder, clip->start + start,
            &input_size, decoded, decoded, &output_size,
            TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        if (status != TINFL_STATUS_DONE || output_size != DISPLAY_FRAME_BYTES || input_size != end - start) {
            ESP_LOGE(TAG, "Invalid compressed frame %u", (unsigned)index);
            abort();
        }
        ESP_ERROR_CHECK(display_present(decoded, DISPLAY_FRAME_BYTES));
        ++sent;

        // Use wall time, so a slower SPI configuration drops stale frames
        // instead of making a ten-second motion take a minute to play.
        int64_t deadline = epoch + (int64_t)((sequence + 1) * 1000000 / clip->fps);
        int64_t now = esp_timer_get_time();
        if (now - last_log >= 10000000) {
            ESP_LOGI(TAG, "Playing %s frame %u/%u; sent=%u in %.1fs",
                     clip->name, (unsigned)index, clip->frame_count, sent, (now - epoch) / 1000000.0);
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
