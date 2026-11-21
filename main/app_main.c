#include "DSPetActionState.h"
#include "DSPetAnimation.h"
#include "DSPetButton.h"
#include "DSPetPanel.h"
#include "DSPetPetPage.h"
#include "asset_config.h"
#include "audio_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <inttypes.h>

static const char *const kTag = "pet";

static void runPet(void)
{
#ifdef CONFIG_DSPET_AUTO_PLAY
    const bool automatic = true;
#else
    const bool automatic = false;
#endif
    ESP_ERROR_CHECK(DSPetAnimationInit(kDSPetPanelWidthPx * kDSPetPanelHeightPx));
    ESP_LOGI(kTag, "Free internal heap after decoder init: %u bytes",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(kTag, "BOOT switches through %u clips", (unsigned)PET_CLIP_COUNT);

    DSPetActionState action = {.startedUs = esp_timer_get_time()};
    uint32_t presses = 0;
    for (;;) {
        DSPetButtonTakeCount(&presses);
        const DSPetClip *clip = &pet_clips[action.index];
        const int64_t durationUs = (int64_t)clip->frameCount * 1000000 / clip->fps;
        if (DSPetActionStateUpdate(&action, PET_CLIP_COUNT, presses, esp_timer_get_time(), durationUs, automatic)) {
            clip = &pet_clips[action.index];
            ESP_LOGI(kTag, "Action: %s (BOOT presses=%" PRIu32 ")", clip->name, presses);
        }
        int64_t nextFrameUs;
        ESP_ERROR_CHECK(DSPetPetPageDraw(clip, action.startedUs, &nextFrameUs));
        // 按绝对时间调度，刷屏较慢时允许跳帧，保留原动作时长。
        int64_t remainingUs = nextFrameUs - esp_timer_get_time();
        TickType_t delayTicks = remainingUs > 0 ? pdMS_TO_TICKS((remainingUs + 999) / 1000) : 1;
        vTaskDelay(delayTicks > 0 ? delayTicks : 1);
    }
}

void app_main(void)
{
    DSPetPanelInit();
#ifdef CONFIG_DSPET_LCD_COLOR_TEST
    const uint16_t colors[] = {0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000};
    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i) {
        DSPetPanelFill(colors[i]);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif
#ifdef CONFIG_DSPET_AUDIO_CHECK
    audio_check_run();
#endif
    ESP_ERROR_CHECK(DSPetButtonStart());
    runPet();
}
