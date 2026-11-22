#include "DSPetButton.h"

#include <stdatomic.h>

#include "button_gpio.h"
#include "driver/gpio.h"
#include "iot_button.h"

enum { kBootPin = GPIO_NUM_0, kShortLimitMs = 1000, kLongPressMs = 5000 };

static button_handle_t sButton;
static atomic_uint     sShortCount;
static atomic_uint     sLongCount;
static atomic_bool     sLongPress;
static atomic_bool     sIgnoreInitial;

static void onRelease(void *button, void *context) {
    if (!atomic_exchange(&sIgnoreInitial, false) && iot_button_get_pressed_time(button) < kShortLimitMs) {
        atomic_fetch_add(&sShortCount, 1);
    }
}

static void onLongPress(void *button, void *context) {
    if (!atomic_load(&sIgnoreInitial)) {
        atomic_fetch_add(&sLongCount, 1);
        atomic_store(&sLongPress, true);
    }
}

esp_err_t DSPetButtonStart(void) {
    if (sButton) {
        return ESP_ERR_INVALID_STATE;
    }
    const button_config_t      config = {.long_press_time = kLongPressMs};
    const button_gpio_config_t gpio   = {.gpio_num = kBootPin, .active_level = 0};
    esp_err_t                  error  = iot_button_new_gpio_device(&config, &gpio, &sButton);
    if (error != ESP_OK) {
        return error;
    }
    atomic_store(&sIgnoreInitial, DSPetButtonIsPressed());

    // 每次独立处理按键，避免 SDK 连击窗口把紧接短按的长按当作第二次点击。
    error = iot_button_set_param(sButton, BUTTON_SHORT_PRESS_TIME_MS, NULL);
    if (error == ESP_OK) {
        error = iot_button_register_cb(sButton, BUTTON_PRESS_UP, NULL, onRelease, NULL);
    }
    if (error == ESP_OK) {
        error = iot_button_register_cb(sButton, BUTTON_LONG_PRESS_START, NULL, onLongPress, NULL);
    }
    if (error != ESP_OK) {
        iot_button_delete(sButton);
        sButton = NULL;
    }
    return error;
}

bool DSPetButtonTakeCount(uint32_t *count) {
    static uint32_t consumed;
    const uint32_t  current = atomic_load(&sShortCount);
    if (current == consumed) {
        return false;
    }
    consumed = current;
    *count   = current;
    return true;
}

bool DSPetButtonTakeLongPress(void) { return atomic_exchange(&sLongPress, false); }

bool DSPetButtonIsPressed(void) { return gpio_get_level(kBootPin) == 0; }

DSPetButtonCounts DSPetButtonGetCounts(void) {
    return (DSPetButtonCounts) {.shortPresses = atomic_load(&sShortCount), .longPresses = atomic_load(&sLongCount)};
}
