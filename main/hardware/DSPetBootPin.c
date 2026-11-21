#include "DSPetBootPin.h"
#include "driver/gpio.h"

enum { kBootPin = GPIO_NUM_0 };

esp_err_t DSPetBootPinInit(void)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << kBootPin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    return gpio_config(&config);
}

bool DSPetBootPinIsPressed(void)
{
    return gpio_get_level(kBootPin) == 0;
}
