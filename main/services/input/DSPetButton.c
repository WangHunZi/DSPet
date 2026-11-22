#include "DSPetButton.h"

#include "DSPetBootPin.h"
#include "DSPetButtonFilter.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static QueueHandle_t     sPresses;
static const char *const kTag = "button";

static void pollButton(void *context) {
    QueueHandle_t     queue = context;
    DSPetButtonFilter filter;
    DSPetButtonFilterInit(&filter, DSPetBootPinIsPressed(), (uint32_t) (esp_timer_get_time() / 1000));
    uint32_t   count    = 0;
    TickType_t lastWake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(10));
        bool     pressed = DSPetBootPinIsPressed();
        uint32_t nowMs   = (uint32_t) (esp_timer_get_time() / 1000);
        if (DSPetButtonFilterUpdate(&filter, pressed, nowMs)) {
            // 保留最新累计值，应用忙于刷屏时仍能还原按下次数。
            ++count;
            xQueueOverwrite(queue, &count);
        }
    }
}

esp_err_t DSPetButtonStart(void) {
    if (sPresses) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(DSPetBootPinInit(), kTag, "init BOOT");
    QueueHandle_t queue = xQueueCreate(1, sizeof(uint32_t));
    if (!queue) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(pollButton, "boot-button", 2048, queue, 3, NULL) != pdPASS) {
        vQueueDelete(queue);
        return ESP_ERR_NO_MEM;
    }
    sPresses = queue;
    return ESP_OK;
}

bool DSPetButtonTakeCount(uint32_t *count) { return sPresses && count && xQueueReceive(sPresses, count, 0) == pdTRUE; }
