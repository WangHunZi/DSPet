#include <inttypes.h>
#include <string.h>

#include "DSPetActionState.h"
#include "DSPetAnimation.h"
#include "DSPetAudioCheck.h"
#include "DSPetButton.h"
#include "DSPetConsole.h"
#include "DSPetPanel.h"
#include "DSPetPetPage.h"
#include "DSPetPlaybackDemo.h"
#include "asset_config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#ifdef CONFIG_DSPET_LOCAL_VOICE
#include "DSPetLocalVoice.h"
#endif

static const char *const kTag = "pet";

#ifdef CONFIG_DSPET_LOCAL_VOICE
// 挥手素材从第 3 秒起已进入笑脸动作，也正好落在关键帧上。
enum { kSpeakingCueSeconds = 3 };
#endif

#if defined(CONFIG_DSPET_PLAYBACK_DEMO) || defined(CONFIG_DSPET_LOCAL_VOICE)
static const DSPetClip *findClip(const char *name) {
    for (size_t i = 0; i < PET_CLIP_COUNT; ++i) {
        if (strcmp(pet_clips[i].name, name) == 0) {
            return &pet_clips[i];
        }
    }
    return &pet_clips[0];
}
#endif

static void runPet(void) {
#ifdef CONFIG_DSPET_AUTO_PLAY
    const bool automatic = true;
#else
    const bool automatic = false;
#endif
    ESP_ERROR_CHECK(DSPetAnimationInit(kDSPetPanelWidthPx * kDSPetPanelHeightPx));
    ESP_LOGI(kTag,
             "Free internal heap after decoder init: %u bytes",
             (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    bool demoEnabled = false;
#ifdef CONFIG_DSPET_LOCAL_VOICE
    ESP_ERROR_CHECK_WITHOUT_ABORT(DSPetLocalVoiceInit());
    const DSPetClip *listening      = findClip("stretch");
    const DSPetClip *waiting        = findClip("idle");
    const DSPetClip *speaking       = findClip("wave");
    const unsigned   speakingCue    = kSpeakingCueSeconds * speaking->fps;
    DSPetVoiceState  lastVoice      = kDSPetVoiceIdle;
    int64_t          voiceStartedUs = 0;
#endif
#ifdef CONFIG_DSPET_SERIAL_SHELL
    ESP_ERROR_CHECK_WITHOUT_ABORT(DSPetConsoleStart());
#endif
#ifdef CONFIG_DSPET_PLAYBACK_DEMO
    esp_err_t demoResult = DSPetPlaybackDemoInit();
    demoEnabled          = demoResult == ESP_OK;
    if (demoEnabled) {
        ESP_LOGI(kTag, "BOOT plays or stops the speech demo");
    } else {
        ESP_LOGW(kTag, "Speech demo unavailable: %s", esp_err_to_name(demoResult));
    }
    const DSPetClip *speaking = findClip("wave");
#endif
#ifdef CONFIG_DSPET_LOCAL_VOICE
    if (DSPetLocalVoiceGetMode() != kDSPetLocalVoiceOff) {
        ESP_LOGI(kTag, "BOOT starts a voice session; say the wake word for hands-free use");
    } else
#endif
        if (!demoEnabled) {
        ESP_LOGI(kTag, "BOOT switches through %u clips", (unsigned) PET_CLIP_COUNT);
    }

    DSPetActionState action  = {.startedUs = esp_timer_get_time()};
    uint32_t         presses = 0;
    for (;;) {
        DSPetButtonTakeCount(&presses);
#ifdef CONFIG_DSPET_LOCAL_VOICE
        if (presses && DSPetLocalVoiceGetMode() != kDSPetLocalVoiceOff) {
            DSPetLocalVoiceStart();
            presses = 0;
        }
#endif
#ifdef CONFIG_DSPET_PLAYBACK_DEMO
        if (demoEnabled) {
            DSPetPlaybackDemoUpdate(presses);
        }
#endif
        const DSPetClip *clip          = &pet_clips[action.index];
        const int64_t    durationUs    = (int64_t) clip->frameCount * 1000000 / clip->fps;
        uint32_t         actionPresses = demoEnabled ? 0 : presses;
        if (DSPetActionStateUpdate(
                &action, PET_CLIP_COUNT, actionPresses, esp_timer_get_time(), durationUs, automatic)) {
            clip = &pet_clips[action.index];
            ESP_LOGI(kTag, "Action: %s (BOOT presses=%" PRIu32 ")", clip->name, actionPresses);
        }
        int64_t  startedUs  = action.startedUs;
        unsigned firstFrame = 0;
#ifdef CONFIG_DSPET_PLAYBACK_DEMO
        if (demoEnabled && DSPetPlaybackDemoSpeaking(&startedUs)) {
            clip = speaking;
        }
#endif
#ifdef CONFIG_DSPET_LOCAL_VOICE
        const DSPetVoiceState voice = DSPetLocalVoiceGetState();
        if (voice != lastVoice) {
            lastVoice      = voice;
            voiceStartedUs = esp_timer_get_time();
        }
        if (voice != kDSPetVoiceIdle) {
            startedUs = voiceStartedUs;
            clip      = voice == kDSPetVoiceListening ? listening : voice == kDSPetVoiceWaiting ? waiting : speaking;
            if (voice == kDSPetVoiceSpeaking && speakingCue < speaking->frameCount) {
                firstFrame = speakingCue;
            }
        }
#endif
        int64_t nextFrameUs;
        ESP_ERROR_CHECK(DSPetPetPageDraw(clip, firstFrame, startedUs, &nextFrameUs));
        // 按绝对时间调度，刷屏较慢时允许跳帧，保留原动作时长。
        int64_t    remainingUs = nextFrameUs - esp_timer_get_time();
        TickType_t delayTicks  = remainingUs > 0 ? pdMS_TO_TICKS((remainingUs + 999) / 1000) : 1;
        vTaskDelay(delayTicks > 0 ? delayTicks : 1);
    }
}

void app_main(void) {
    DSPetPanelInit();
#ifdef CONFIG_DSPET_LCD_COLOR_TEST
    const uint16_t colors[] = {0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000};
    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i) {
        DSPetPanelFill(colors[i]);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif
#ifdef CONFIG_DSPET_AUDIO_CHECK
    DSPetAudioCheckRun();
#endif
    ESP_ERROR_CHECK(DSPetButtonStart());
    runPet();
}
