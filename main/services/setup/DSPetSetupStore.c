#include "DSPetSetupStore.h"

#include <stdint.h>

#include "mbedtls/platform_util.h"
#include "nvs.h"
#include "nvs_flash.h"

enum { kRecordVersion = 1 };
static const char kNamespace[] = "dspet_setup";
static const char kRecordKey[] = "credentials";

typedef struct {
    uint8_t          version;
    DSPetSetupConfig config;
} SetupRecord;

static esp_err_t openStore(nvs_open_mode_t mode, nvs_handle_t *handle) {
    const esp_err_t error = nvs_flash_init();
    return error == ESP_OK ? nvs_open(kNamespace, mode, handle) : error;
}

esp_err_t DSPetSetupStoreLoad(DSPetSetupConfig *config) {
    nvs_handle_t handle;
    esp_err_t    error = openStore(NVS_READONLY, &handle);
    if (error != ESP_OK) {
        return error;
    }
    SetupRecord record = {0};
    size_t      length = sizeof(record);
    error              = nvs_get_blob(handle, kRecordKey, &record, &length);
    nvs_close(handle);
    if (error == ESP_OK) {
        if (length != sizeof(record) || record.version != kRecordVersion || !DSPetSetupConfigValid(&record.config)) {
            error = ESP_ERR_INVALID_RESPONSE;
        } else {
            *config = record.config;
        }
    }
    mbedtls_platform_zeroize(&record, sizeof(record));
    return error;
}

esp_err_t DSPetSetupStoreSave(const DSPetSetupConfig *config) {
    if (!DSPetSetupConfigValid(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t    error = openStore(NVS_READWRITE_PURGE, &handle);
    if (error != ESP_OK) {
        return error;
    }
    SetupRecord record = {.version = kRecordVersion, .config = *config};
    error              = nvs_set_blob(handle, kRecordKey, &record, sizeof(record));
    mbedtls_platform_zeroize(&record, sizeof(record));
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error;
}

esp_err_t DSPetSetupStoreClear(void) {
    nvs_handle_t handle;
    esp_err_t    error = openStore(NVS_READWRITE_PURGE, &handle);
    if (error != ESP_OK) {
        return error;
    }
    error = nvs_erase_all(handle);
    if (error == ESP_OK) {
        error = nvs_purge_all(handle);
    }
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error;
}
