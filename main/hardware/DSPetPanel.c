#include "DSPetPanel.h"

#include <assert.h>
#include <string.h>

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
#include "sdkconfig.h"

// 使用芯片 GPIO 编号。D9 作片选，屏幕只写。
enum { kPinBacklight = 6, kPinReset = 5, kPinDc = 4, kPinMosi = 9, kPinClock = 7, kPinCs = 8 };

#ifdef CONFIG_DSPET_DIRECT_TEST
enum { kDmaRows = 30 };
#else
enum { kDmaRows = kDSPetPanelHeightPx };
#endif
enum { kDmaBytes = kDSPetPanelWidthPx * 2 * kDmaRows };
_Static_assert(kDSPetPanelHeightPx % kDmaRows == 0, "DMA bands must cover the entire panel");

static esp_lcd_panel_handle_t sPanel;
static uint8_t               *sDmaBand;
static SemaphoreHandle_t      sTransferDone;
static bool                   sTimedOut;
static const char *const      kTag = "display";

static bool colorDone(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event, void *context) {
    BaseType_t wakeTask = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t) context, &wakeTask);
    return wakeTask == pdTRUE;
}

static esp_err_t sendBand(int y) {
    esp_err_t err = esp_lcd_panel_draw_bitmap(sPanel, 0, y, kDSPetPanelWidthPx, y + kDmaRows, sDmaBand);
    if (err != ESP_OK) {
        return err;
    }
    // DMA 完成通知到达前，发送缓冲不能复用。
    if (xSemaphoreTake(sTransferDone, pdMS_TO_TICKS(3000)) != pdTRUE) {
        sTimedOut = true;
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void DSPetPanelFill(uint16_t rgb565) {
    ESP_ERROR_CHECK(sPanel && sDmaBand && !sTimedOut ? ESP_OK : ESP_ERR_INVALID_STATE);
    for (size_t i = 0; i < kDmaBytes; i += 2) {
        sDmaBand[i]     = rgb565 >> 8;
        sDmaBand[i + 1] = rgb565 & 0xFF;
    }
    for (int y = 0; y < kDSPetPanelHeightPx; y += kDmaRows) {
        ESP_ERROR_CHECK(sendBand(y));
    }
}

esp_err_t DSPetPanelPresent(const uint8_t *pixels, size_t sizeBytes) {
    if (!sPanel || sTimedOut) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!pixels || sizeBytes != kDSPetPanelFrameBytes) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int y = 0; y < kDSPetPanelHeightPx; y += kDmaRows, pixels += kDmaBytes) {
        memcpy(sDmaBand, pixels, kDmaBytes);
        esp_err_t error = sendBand(y);
        if (error != ESP_OK) {
            return error;
        }
    }
    return ESP_OK;
}

void DSPetPanelInit(void) {
    assert(!sPanel);
    ESP_LOGI(kTag,
             "240x240 SPI=%dMHz mode=%d Y=%d",
             CONFIG_DSPET_LCD_SPI_MHZ,
             CONFIG_DSPET_LCD_SPI_MODE,
             CONFIG_DSPET_LCD_Y_GAP);
    gpio_config_t backlight = {
        .pin_bit_mask = 1ULL << kPinBacklight,
        .mode         = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&backlight));
    ESP_ERROR_CHECK(gpio_set_level(kPinBacklight, 0));
    // 直连测试将内部 DMA 内存留给 SPI 和 I²S，把整帧发送拆为若干条。
    sDmaBand      = heap_caps_malloc(kDmaBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    sTransferDone = xSemaphoreCreateBinary();
    assert(sDmaBand && sTransferDone);

    spi_bus_config_t bus = {
        .mosi_io_num     = kPinMosi,
        .miso_io_num     = -1,
        .sclk_io_num     = kPinClock,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = kDmaBytes,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_handle_t     io;
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num         = kPinCs,
        .dc_gpio_num         = kPinDc,
        .spi_mode            = CONFIG_DSPET_LCD_SPI_MODE,
        .pclk_hz             = CONFIG_DSPET_LCD_SPI_MHZ * 1000000,
        .trans_queue_depth   = 1,
        .lcd_cmd_bits        = 8,
        .lcd_param_bits      = 8,
        .on_color_trans_done = colorDone,
        .user_ctx            = sTransferDone,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &io));
    esp_lcd_panel_dev_config_t config = {
        .reset_gpio_num = kPinReset,
#ifdef CONFIG_DSPET_LCD_BGR
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#endif
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &config, &sPanel));
    ESP_ERROR_CHECK(gpio_set_level(kPinReset, 1));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(gpio_set_level(kPinReset, 0));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(gpio_set_level(kPinReset, 1));
    vTaskDelay(pdMS_TO_TICKS(150));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0x01, NULL, 0));
    vTaskDelay(pdMS_TO_TICKS(150));
    ESP_ERROR_CHECK(esp_lcd_panel_init(sPanel));

    // 沿用调试版上板验证过的初始化参数，来源见 THIRD_PARTY.md。
    static const struct {
        uint8_t command, sizeBytes, data[14];
    } settings[] = {
        {0xB2, 5, {0x0C, 0x0C, 0x00, 0x33, 0x33}},
        {0xB7, 1, {0x35}},
        {0xBB, 1, {0x19}},
        {0xC0, 1, {0x2C}},
        {0xC2, 1, {0x01}},
        {0xC3, 1, {0x12}},
        {0xC4, 1, {0x20}},
        {0xC6, 1, {0x0F}},
        {0xD0, 2, {0xA4, 0xA1}},
        {0xE0, 14, {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23}},
        {0xE1, 14, {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23}},
    };
    for (size_t i = 0; i < sizeof(settings) / sizeof(settings[0]); ++i)
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, settings[i].command, settings[i].data, settings[i].sizeBytes));
#ifdef CONFIG_DSPET_LCD_INVERT
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(sPanel, true));
#else
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(sPanel, false));
#endif
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(sPanel, 0, CONFIG_DSPET_LCD_Y_GAP));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0x13, NULL, 0));
    vTaskDelay(pdMS_TO_TICKS(20));
    DSPetPanelFill(0x0000);
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(sPanel, true));
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_ERROR_CHECK(gpio_set_level(kPinBacklight, 1));
}
