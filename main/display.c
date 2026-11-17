#include "display.h"
#include <assert.h>
#include <string.h>
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// GPIO numbers, not XIAO D numbers. D9 is CS; MISO is disabled.
enum { PIN_BL = 6, PIN_RST = 5, PIN_DC = 4,
       PIN_MOSI = 9, PIN_SCK = 7, PIN_CS = 8 };

static esp_lcd_panel_handle_t panel;
static uint8_t *dma_frame;
static SemaphoreHandle_t transfer_done;
static const char *TAG = "display";

static bool color_done(esp_lcd_panel_io_handle_t io,
                       esp_lcd_panel_io_event_data_t *event, void *ctx)
{
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)ctx, &wake);
    return wake == pdTRUE;
}

static esp_err_t send_frame(void)
{
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel, 0, 0,
                                             DISPLAY_WIDTH, DISPLAY_HEIGHT, dma_frame);
    if (err != ESP_OK) return err;
    // At 1 MHz a full frame needs ~922 ms. The buffer stays owned by DMA
    // until this callback, even when the caller wants to display another frame.
    if (xSemaphoreTake(transfer_done, pdMS_TO_TICKS(3000)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    return ESP_OK;
}

void display_fill(uint16_t rgb565)
{
    assert(panel && dma_frame);
    for (size_t i = 0; i < DISPLAY_FRAME_BYTES; i += 2) {
        dma_frame[i] = rgb565 >> 8;
        dma_frame[i + 1] = rgb565 & 0xFF;
    }
    ESP_ERROR_CHECK(send_frame());
}

esp_err_t display_present(const uint8_t *pixels, size_t size)
{
    if (!panel) return ESP_ERR_INVALID_STATE;
    if (!pixels || size != DISPLAY_FRAME_BYTES) return ESP_ERR_INVALID_ARG;
    memcpy(dma_frame, pixels, size);
    return send_frame();
}

void display_init(void)
{
    assert(!panel);
    ESP_LOGI(TAG, "240x240 SPI=%dMHz mode=%d Y=%d", CONFIG_DSPET_LCD_SPI_MHZ,
             CONFIG_DSPET_LCD_SPI_MODE, CONFIG_DSPET_LCD_Y_GAP);
    gpio_config_t backlight = {
        .pin_bit_mask = 1ULL << PIN_BL, .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&backlight));
    ESP_ERROR_CHECK(gpio_set_level(PIN_BL, 0));
    dma_frame = heap_caps_malloc(DISPLAY_FRAME_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    transfer_done = xSemaphoreCreateBinary();
    assert(dma_frame && transfer_done);

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_MOSI, .miso_io_num = -1, .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_FRAME_BYTES,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = PIN_CS, .dc_gpio_num = PIN_DC,
        .spi_mode = CONFIG_DSPET_LCD_SPI_MODE,
        .pclk_hz = CONFIG_DSPET_LCD_SPI_MHZ * 1000000,
        .trans_queue_depth = 1, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
        .on_color_trans_done = color_done, .user_ctx = transfer_done,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &io));
    esp_lcd_panel_dev_config_t config = {
        .reset_gpio_num = PIN_RST,
#ifdef CONFIG_DSPET_LCD_BGR
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#endif
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &config, &panel));
    ESP_ERROR_CHECK(gpio_set_level(PIN_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(gpio_set_level(PIN_RST, 0));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(gpio_set_level(PIN_RST, 1));
    vTaskDelay(pdMS_TO_TICKS(150));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0x01, NULL, 0));
    vTaskDelay(pdMS_TO_TICKS(150));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    // Supplemental settings retained from the working diagnostic firmware.
    // Reference: Waveshare Pico-LCD-1.3 init_display (see THIRD_PARTY.md).
    static const struct { uint8_t command, size, data[14]; } settings[] = {
        {0xB2, 5, {0x0C,0x0C,0x00,0x33,0x33}},
        {0xB7, 1, {0x35}}, {0xBB, 1, {0x19}}, {0xC0, 1, {0x2C}},
        {0xC2, 1, {0x01}}, {0xC3, 1, {0x12}}, {0xC4, 1, {0x20}},
        {0xC6, 1, {0x0F}}, {0xD0, 2, {0xA4,0xA1}},
        {0xE0,14,{0xD0,0x04,0x0D,0x11,0x13,0x2B,0x3F,0x54,0x4C,0x18,0x0D,0x0B,0x1F,0x23}},
        {0xE1,14,{0xD0,0x04,0x0C,0x11,0x13,0x2C,0x3F,0x44,0x51,0x2F,0x1F,0x1F,0x20,0x23}},
    };
    for (size_t i = 0; i < sizeof(settings) / sizeof(settings[0]); ++i)
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, settings[i].command,
                                                 settings[i].data, settings[i].size));
#ifdef CONFIG_DSPET_LCD_INVERT
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
#else
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, false));
#endif
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, 0, CONFIG_DSPET_LCD_Y_GAP));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0x13, NULL, 0));
    vTaskDelay(pdMS_TO_TICKS(20));
    display_fill(0x0000);
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_ERROR_CHECK(gpio_set_level(PIN_BL, 1));
}
