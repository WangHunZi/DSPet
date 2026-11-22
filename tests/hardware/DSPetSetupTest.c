#include "DSPetSetupTest.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "DSPetButton.h"
#include "DSPetSetupStore.h"
#include "nvs.h"

esp_err_t DSPetSetupTestStorage(void) {
    static const char      kTestNamespace[] = "dspet_test";
    const DSPetSetupConfig saved            = {.ssid = "DSPet-Test", .password = "test-pass", .key = "sk-test-only"};
    DSPetSetupConfig       actual;
    esp_err_t              error = DSPetSetupStoreSave(kTestNamespace, &saved);
    if (error == ESP_OK) {
        error = DSPetSetupStoreLoad(kTestNamespace, &actual);
        if (error == ESP_OK && memcmp(&actual, &saved, sizeof(actual)) != 0) {
            error = ESP_ERR_INVALID_RESPONSE;
        }
    }
    const esp_err_t cleared = DSPetSetupStoreClear(kTestNamespace);
    if (error == ESP_OK) {
        error = cleared;
    }
    if (error == ESP_OK && DSPetSetupStoreLoad(kTestNamespace, &actual) != ESP_ERR_NVS_NOT_FOUND) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    printf(
        "DSPET_TEST_SETUP case=storage state=%s error=%s\n", error == ESP_OK ? "pass" : "fail", esp_err_to_name(error));
    return error;
}

void DSPetSetupTestPrintButton(void) {
    const DSPetButtonCounts counts = DSPetButtonGetCounts();
    printf("DSPET_TEST_SETUP case=button held=%u short=%" PRIu32 " long=%" PRIu32 "\n",
           DSPetButtonIsPressed() ? 1u : 0u,
           counts.shortPresses,
           counts.longPresses);
}
