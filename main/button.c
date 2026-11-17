#include "button.h"
#include "button_filter.h"
#include <assert.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static QueueHandle_t presses;

static void poll_button(void *ctx)
{
    button_filter_t filter;
    button_filter_init(&filter, gpio_get_level(GPIO_NUM_0) == 0,
                       (uint32_t)(esp_timer_get_time() / 1000));
    uint32_t count = 0;
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
        bool pressed = gpio_get_level(GPIO_NUM_0) == 0;
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (button_filter_update(&filter, pressed, now_ms)) {
            ++count;
            // Keep the latest count even while SPI is busy; rapid presses
            // are coalesced without losing the final selected action.
            xQueueOverwrite(presses, &count);
        }
    }
}

void button_start(void)
{
    assert(!presses);
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << GPIO_NUM_0,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    presses = xQueueCreate(1, sizeof(uint32_t));
    assert(presses);
    BaseType_t result = xTaskCreate(poll_button, "boot-button", 2048, NULL, 3, NULL);
    assert(result == pdPASS);
}

bool button_take_count(uint32_t *count)
{
    assert(presses && count);
    return xQueueReceive(presses, count, 0) == pdTRUE;
}
