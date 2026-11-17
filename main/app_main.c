#include "display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    display_init();
    const uint16_t colors[] = {0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000};
    for (;;) {
        for (unsigned i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i) {
            display_fill(colors[i]);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}
