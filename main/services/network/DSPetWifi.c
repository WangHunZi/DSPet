#include "DSPetWifi.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "mbedtls/platform_util.h"
#include "nvs_flash.h"

enum { kGotIp = BIT0, kDisconnected = BIT1, kJoinTimeoutMs = 17000 };

static esp_netif_t                 *sSta;
static EventGroupHandle_t           sEvents;
static esp_event_handler_instance_t sIpHandler;
static esp_event_handler_instance_t sDisconnectHandler;
static bool                         sInitialized;
static bool                         sDriverReady;
static bool                         sStarted;

static void networkEvent(void *arg, esp_event_base_t base, int32_t id, void *data) {
    EventGroupHandle_t events = arg;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(events, kGotIp);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(events, kGotIp);
        const wifi_event_sta_disconnected_t *event = data;
        printf("DSPET_WIFI_DISCONNECT reason=%u\n", (unsigned) event->reason);
        xEventGroupSetBits(events, kDisconnected);
    }
}

bool DSPetWifiIsConnected(void) { return sEvents && (xEventGroupGetBits(sEvents) & kGotIp); }

esp_err_t DSPetWifiOpen(void) {
    if (sSta) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!sInitialized) {
        // 射频校准使用 NVS。STA 凭据仍按 WIFI_STORAGE_RAM 配置。
        esp_err_t error = nvs_flash_init();
        if (error != ESP_OK) {
            return error;
        }
        error = esp_netif_init();
        if (error != ESP_OK) {
            return error;
        }
        error = esp_event_loop_create_default();
        if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
            return error;
        }
        sInitialized = true;
    }

    sSta            = esp_netif_create_default_wifi_sta();
    sEvents         = xEventGroupCreate();
    esp_err_t error = sSta && sEvents ? ESP_OK : ESP_ERR_NO_MEM;
    if (error == ESP_OK) {
        wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
        config.nvs_enable         = 0;
        error                     = esp_wifi_init(&config);
        sDriverReady              = error == ESP_OK;
    }
    if (error == ESP_OK) {
        error = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    }
    if (error == ESP_OK) {
        error = esp_wifi_set_mode(WIFI_MODE_STA);
    }
    if (error == ESP_OK) {
        error = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20);
    }
    if (error == ESP_OK) {
        error = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, networkEvent, sEvents, &sIpHandler);
    }
    if (error == ESP_OK) {
        error = esp_event_handler_instance_register(
            WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, networkEvent, sEvents, &sDisconnectHandler);
    }
    if (error == ESP_OK) {
        error    = esp_wifi_start();
        sStarted = error == ESP_OK;
    }
    if (error == ESP_OK) {
        error = esp_wifi_set_ps(WIFI_PS_NONE);
    }
    if (error != ESP_OK) {
        DSPetWifiClose();
    }
    return error;
}

esp_err_t DSPetWifiScan(const char *prefix) {
    esp_err_t error = esp_wifi_scan_start(NULL, true);
    if (error != ESP_OK) {
        return error;
    }
    uint16_t count = 0;
    error          = esp_wifi_scan_get_ap_num(&count);
    if (error != ESP_OK || !count) {
        esp_wifi_clear_ap_list();
        return error == ESP_OK ? ESP_ERR_NOT_FOUND : error;
    }

    wifi_ap_record_t *records = calloc(count, sizeof(*records));
    if (!records) {
        esp_wifi_clear_ap_list();
        return ESP_ERR_NO_MEM;
    }
    error = esp_wifi_scan_get_ap_records(&count, records);
    if (error != ESP_OK) {
        esp_wifi_clear_ap_list();
        free(records);
        return error;
    }
    const size_t prefixBytes = strlen(prefix);
    bool         found       = false;
    for (size_t i = 0; i < count; ++i) {
        const char *ssid = (const char *) records[i].ssid;
        if (strncmp(ssid, prefix, prefixBytes) == 0) {
            printf("DSPET_WIFI_AP ssid=%s channel=%u rssi=%d\n", ssid, records[i].primary, records[i].rssi);
            found = true;
        }
    }
    free(records);
    return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t DSPetWifiConnect(const char *ssid, const char *password) {
    wifi_config_t config    = {.sta = {.threshold = {.authmode = WIFI_AUTH_WPA2_PSK}, .pmf_cfg = {.capable = true}}};
    const size_t  ssidBytes = strlen(ssid);
    const size_t  passwordBytes = strlen(password);
    if (!ssidBytes || ssidBytes > sizeof(config.sta.ssid) || passwordBytes > sizeof(config.sta.password)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(config.sta.ssid, ssid, ssidBytes);
    memcpy(config.sta.password, password, passwordBytes);
    esp_err_t error = esp_wifi_set_config(WIFI_IF_STA, &config);
    mbedtls_platform_zeroize(&config, sizeof(config));
    if (error != ESP_OK) {
        return error;
    }
    xEventGroupClearBits(sEvents, kGotIp | kDisconnected);
    error = esp_wifi_connect();
    if (error != ESP_OK) {
        return error;
    }
    const EventBits_t events =
        xEventGroupWaitBits(sEvents, kGotIp | kDisconnected, pdFALSE, pdFALSE, pdMS_TO_TICKS(kJoinTimeoutMs));
    if (events & kDisconnected) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (!(events & kGotIp)) {
        return ESP_ERR_TIMEOUT;
    }
    wifi_ap_record_t record;
    error = esp_wifi_sta_get_ap_info(&record);
    if (error == ESP_OK) {
        printf("DSPET_WIFI_CONNECTED channel=%u rssi=%d\n", record.primary, record.rssi);
    }
    return error;
}

void DSPetWifiClose(void) {
    if (sStarted) {
        esp_wifi_stop();
        sStarted = false;
    }
    if (sDisconnectHandler) {
        esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, sDisconnectHandler);
        sDisconnectHandler = NULL;
    }
    if (sIpHandler) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, sIpHandler);
        sIpHandler = NULL;
    }
    if (sDriverReady) {
        esp_wifi_deinit();
        sDriverReady = false;
    }
    if (sSta) {
        esp_netif_destroy_default_wifi(sSta);
        sSta = NULL;
    }
    if (sEvents) {
        vEventGroupDelete(sEvents);
        sEvents = NULL;
    }
}
