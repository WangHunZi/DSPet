#include "DSPetSetup.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "DSPetLocalVoice.h"
#include "DSPetSetupPage.h"
#include "DSPetSetupPortal.h"
#include "DSPetSetupStore.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/platform_util.h"
#include "nvs.h"

typedef enum {
    kSetupConnecting,
    kSetupPortal,
    kSetupOnline,
    kSetupResetting,
    kSetupError,
} SetupState;

static const char *const kTag   = "setup";
static atomic_int        sState = kSetupError;
static DSPetSetupHotspot sHotspot;

static esp_err_t startPortal(void) {
    esp_err_t error = DSPetSetupPortalStart(&sHotspot);
    if (error != ESP_OK) {
        atomic_store(&sState, kSetupError);
        return error;
    }
    atomic_store(&sState, kSetupPortal);
    DSPetSetupPrintStatus();
    return DSPetSetupPageDraw(&sHotspot);
}

esp_err_t DSPetSetupStart(void) {
    DSPetSetupConfig config = {0};
    esp_err_t        error  = DSPetSetupStoreLoad(kDSPetSetupNamespace, &config);
    if (error == ESP_OK) {
        DSPetDirectSettings settings = {0};
        _Static_assert(sizeof(settings.ssid) == sizeof(config.ssid), "SSID capacity");
        _Static_assert(sizeof(settings.password) == sizeof(config.password), "Password capacity");
        _Static_assert(sizeof(settings.key) == sizeof(config.key), "Key capacity");
        memcpy(settings.ssid, config.ssid, sizeof(config.ssid));
        memcpy(settings.password, config.password, sizeof(config.password));
        memcpy(settings.key, config.key, sizeof(config.key));
        error = DSPetLocalVoiceEnableDirect(&settings);
        mbedtls_platform_zeroize(&settings, sizeof(settings));
    }
    mbedtls_platform_zeroize(&config, sizeof(config));
    if (error == ESP_OK) {
        atomic_store(&sState, kSetupConnecting);
        DSPetSetupPrintStatus();
        return ESP_OK;
    }
    if (error != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(kTag, "Load or start saved setup: %s", esp_err_to_name(error));
    }
    return startPortal();
}

bool DSPetSetupUpdate(bool reset, bool buttonHeld) {
    SetupState state = atomic_load(&sState);
    if (reset && state != kSetupResetting) {
        if (state == kSetupPortal) {
            atomic_store(&sState, kSetupError);
            DSPetSetupPortalStop();
        }
        const esp_err_t error = DSPetSetupStoreClear(kDSPetSetupNamespace);
        printf("DSPET_SETUP_RESET result=%s\n", esp_err_to_name(error));
        if (error == ESP_OK) {
            state = kSetupResetting;
            atomic_store(&sState, state);
        } else if (state == kSetupPortal) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(startPortal());
        }
    }
    if (state == kSetupResetting) {
        // BOOT 是启动绑带引脚。擦除已完成，待释放后再重启。
        if (!buttonHeld) {
            esp_restart();
        }
        return true;
    }
    if (state == kSetupConnecting) {
        const DSPetLocalVoiceMode mode = DSPetLocalVoiceGetMode();
        if (mode == kDSPetLocalVoiceOff) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(startPortal());
        } else if (mode == kDSPetLocalVoiceOn) {
            atomic_store(&sState, kSetupOnline);
            DSPetSetupPrintStatus();
        }
    } else if (state == kSetupPortal && DSPetSetupPortalPoll()) {
        puts("DSPET_SETUP_SAVED result=ESP_OK");
        vTaskDelay(pdMS_TO_TICKS(450));
        DSPetSetupPortalStop();
        esp_restart();
    }
    return atomic_load(&sState) == kSetupPortal;
}

void DSPetSetupPrintStatus(void) {
    const char *const names[] = {
        [kSetupConnecting] = "connecting",
        [kSetupPortal]     = "portal",
        [kSetupOnline]     = "online",
        [kSetupResetting]  = "resetting",
        [kSetupError]      = "error",
    };
    const SetupState state = atomic_load(&sState);
    if (state == kSetupPortal) {
        printf("DSPET_SETUP state=portal ssid=%s url=http://" DSPET_SETUP_PORTAL_ADDRESS "/\n", sHotspot.ssid);
    } else {
        printf("DSPET_SETUP state=%s\n", names[state]);
    }
}

void DSPetSetupPrintHotspot(void) {
    if (atomic_load(&sState) == kSetupPortal) {
        printf("DSPET_SETUP_HOTSPOT ssid=%s password=%s\n", sHotspot.ssid, sHotspot.password);
    } else {
        puts("DSPET_SETUP_HOTSPOT unavailable");
    }
}
