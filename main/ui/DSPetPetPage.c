#include "DSPetPetPage.h"

#include "DSPetAnimation.h"
#include "DSPetPanel.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *const kTag       = "pet";
static int64_t           sStartedUs = -1;
static int64_t           sLastLogUs;
static unsigned          sFramesSent;
static int64_t           sDecodeTotalUs;
static int64_t           sTransferTotalUs;

esp_err_t DSPetPetPageDraw(const DSPetClip *clip, int64_t startedUs, int64_t *nextFrameUs) {
    if (!nextFrameUs) {
        return ESP_ERR_INVALID_ARG;
    }
    if (startedUs != sStartedUs) {
        sStartedUs       = startedUs;
        sLastLogUs       = startedUs;
        sFramesSent      = 0;
        sDecodeTotalUs   = 0;
        sTransferTotalUs = 0;
    }
    int64_t             decodeStartUs = esp_timer_get_time();
    DSPetAnimationFrame frame;
    esp_err_t           error = DSPetAnimationFrameAt(clip, startedUs, decodeStartUs, &frame);
    if (error != ESP_OK) {
        return error;
    }
    int64_t transferStartUs = esp_timer_get_time();
    error                   = DSPetPanelPresent(frame.pixels, frame.sizeBytes);
    if (error != ESP_OK) {
        return error;
    }
    int64_t nowUs = esp_timer_get_time();
    sDecodeTotalUs += transferStartUs - decodeStartUs;
    sTransferTotalUs += nowUs - transferStartUs;
    ++sFramesSent;
    if (nowUs - sLastLogUs >= 3000000) {
        ESP_LOGI(kTag,
                 "%s frame=%u/%u fps=%.1f decode=%.2fms SPI=%.2fms",
                 clip->name,
                 frame.index,
                 clip->frameCount,
                 (sFramesSent - 1) * 1000000.0 / (nowUs - startedUs),
                 sDecodeTotalUs / (sFramesSent * 1000.0),
                 sTransferTotalUs / (sFramesSent * 1000.0));
        sLastLogUs = nowUs;
    }
    *nextFrameUs = frame.nextFrameUs;
    return ESP_OK;
}
