#include <assert.h>
#include <string.h>

#include "DSPetSetupForm.h"

int main(void) {
    DSPetSetupConfig config;
    assert(DSPetSetupFormParse(
        "ssid=%E9%B2%B8%E9%B1%BC+2.4G&password=a%2Bb%26c123&token=abc&key=sk-test", "abc", &config));
    assert(strcmp(config.ssid, "鲸鱼 2.4G") == 0);
    assert(strcmp(config.password, "a+b&c123") == 0);
    assert(strcmp(config.key, "sk-test") == 0);

    const char *const rejected[] = {
        "ssid=abc&password=pass1234&key=key",
        "ssid=abc&password=pass1234&token=wrong&key=key",
        "ssid=abc&password=pass1234&token=abc&key=key&ssid=again",
        "ssid=ab%00c&password=pass1234&token=abc&key=key",
        "ssid=ab%&password=pass1234&token=abc&key=key",
        "ssid=123456789012345678901234567890123&password=pass1234&token=abc&key=key",
        "ssid=abc&password=short&token=abc&key=key",
        "ssid=abc&password=pass1234&token=abc&key=key+key",
        "ssid=abc&password=pass1234&token=abc&key=",
    };
    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        assert(!DSPetSetupFormParse(rejected[i], "abc", &config));
    }
    memset(&config, 'x', sizeof(config));
    assert(!DSPetSetupConfigValid(&config));
}
