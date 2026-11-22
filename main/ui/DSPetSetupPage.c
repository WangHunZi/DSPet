#include "DSPetSetupPage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "DSPetPanel.h"
#include "esp_heap_caps.h"
#include "qrcode.h"

static const struct {
    char    character;
    uint8_t rows[7];
} kGlyphs[] = {
    {'0', {14, 17, 19, 21, 25, 17, 14}},
    {'1', {4, 12, 4, 4, 4, 4, 14}},
    {'2', {14, 17, 1, 2, 4, 8, 31}},
    {'3', {30, 1, 1, 14, 1, 1, 30}},
    {'4', {2, 6, 10, 18, 31, 2, 2}},
    {'5', {31, 16, 16, 30, 1, 1, 30}},
    {'6', {14, 16, 16, 30, 17, 17, 14}},
    {'7', {31, 1, 2, 4, 8, 8, 8}},
    {'8', {14, 17, 17, 14, 17, 17, 14}},
    {'9', {14, 17, 17, 15, 1, 1, 14}},
    {'.', {0, 0, 0, 0, 0, 6, 6}},
    {'A', {14, 17, 17, 31, 17, 17, 17}},
    {'C', {14, 17, 16, 16, 16, 17, 14}},
    {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'I', {14, 4, 4, 4, 4, 4, 14}},
    {'N', {17, 25, 21, 19, 17, 17, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},
    {'W', {17, 17, 17, 21, 21, 27, 17}},
};

static void square(uint8_t *pixels, int x, int y, int size) {
    for (int row = y; row < y + size; ++row) {
        memset(pixels + (row * kDSPetPanelWidthPx + x) * 2, 0, size * 2);
    }
}

static void drawLabel(uint8_t *pixels, const char *text, int y) {
    enum { kScale = 2, kAdvance = 6 * kScale };
    int x = (kDSPetPanelWidthPx - (int) strlen(text) * kAdvance + kScale) / 2;
    for (; *text; ++text, x += kAdvance) {
        for (size_t glyph = 0; glyph < sizeof(kGlyphs) / sizeof(kGlyphs[0]); ++glyph) {
            if (kGlyphs[glyph].character != *text) {
                continue;
            }
            for (int row = 0; row < 7; ++row) {
                for (int col = 0; col < 5; ++col) {
                    if (kGlyphs[glyph].rows[row] & (1u << (4 - col))) {
                        square(pixels, x + col * kScale, y + row * kScale, kScale);
                    }
                }
            }
            break;
        }
    }
}

static void drawCode(esp_qrcode_handle_t code, void *context) {
    uint8_t  *pixels  = context;
    const int modules = esp_qrcode_get_size(code);
    const int scale   = 176 / (modules + 8);
    const int left    = (kDSPetPanelWidthPx - modules * scale) / 2;
    const int top     = (kDSPetPanelHeightPx - modules * scale) / 2;
    for (int y = 0; y < modules; ++y) {
        for (int x = 0; x < modules; ++x) {
            if (esp_qrcode_get_module(code, x, y)) {
                square(pixels, left + x * scale, top + y * scale, scale);
            }
        }
    }
}

esp_err_t DSPetSetupPageDraw(const DSPetSetupHotspot *hotspot) {
    uint8_t *pixels = heap_caps_malloc(kDSPetPanelFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) {
        return ESP_ERR_NO_MEM;
    }
    memset(pixels, 0xFF, kDSPetPanelFrameBytes);
    char wifiQr[64];
    snprintf(wifiQr, sizeof(wifiQr), "WIFI:T:WPA;S:%s;P:%s;;", hotspot->ssid, hotspot->password);
    esp_qrcode_config_t config  = ESP_QRCODE_CONFIG_DEFAULT();
    config.max_qrcode_version   = 6;
    config.qrcode_ecc_level     = ESP_QRCODE_ECC_MED;
    config.display_func_with_cb = drawCode;
    config.user_data            = pixels;
    esp_err_t error             = esp_qrcode_generate(&config, wifiQr);
    if (error == ESP_OK) {
        drawLabel(pixels, "SCAN WIFI", 10);
        drawLabel(pixels, DSPET_SETUP_PORTAL_ADDRESS, 214);
        error = DSPetPanelPresent(pixels, kDSPetPanelFrameBytes);
    }
    free(pixels);
    return error;
}
