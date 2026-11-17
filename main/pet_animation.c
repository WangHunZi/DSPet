#include "pet_animation.h"
#include "asset_config.h"
#include "display.h"
#include <assert.h>
#include <inttypes.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern const uint8_t idle_start[] asm("_binary_idle_rgb565_start");
extern const uint8_t idle_end[] asm("_binary_idle_rgb565_end");

_Static_assert(PET_WIDTH == DISPLAY_WIDTH && PET_HEIGHT == DISPLAY_HEIGHT,
               "Animation and display dimensions must match");
_Static_assert(PET_FRAME_BYTES == DISPLAY_FRAME_BYTES, "Invalid frame stride");

void pet_play_idle(void)
{
    static const char *TAG = "pet";
    size_t size = (uintptr_t)idle_end - (uintptr_t)idle_start;
    assert(size == (size_t)PET_FRAME_COUNT * PET_FRAME_BYTES);
    ESP_LOGI(TAG, "Idle animation: %d frames, %d fps, %u flash bytes",
             PET_FRAME_COUNT, PET_FPS, (unsigned)size);
    ESP_LOGI(TAG, "Free internal heap after display init: %u bytes",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    const int64_t epoch = esp_timer_get_time();
    int64_t last_log = epoch;
    unsigned sent = 0;
    for (;;) {
        int64_t elapsed = esp_timer_get_time() - epoch;
        uint64_t sequence = (uint64_t)elapsed * PET_FPS / 1000000;
        size_t index = sequence % PET_FRAME_COUNT;
        ESP_ERROR_CHECK(display_present(idle_start + index * PET_FRAME_BYTES, PET_FRAME_BYTES));
        ++sent;

        // Use wall time, so a slower SPI configuration drops stale frames
        // instead of making a ten-second motion take a minute to play.
        int64_t deadline = epoch + (int64_t)((sequence + 1) * 1000000 / PET_FPS);
        int64_t now = esp_timer_get_time();
        if (now - last_log >= 10000000) {
            ESP_LOGI(TAG, "Playing frame %u/%d; sent=%u in %.1fs",
                     (unsigned)index, PET_FRAME_COUNT, sent, (now - epoch) / 1000000.0);
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
