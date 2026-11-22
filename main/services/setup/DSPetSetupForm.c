#include "DSPetSetupForm.h"

#include <stddef.h>
#include <string.h>

static size_t boundedLength(const char *text, size_t capacity) {
    const char *end = memchr(text, 0, capacity);
    return end ? (size_t) (end - text) : capacity;
}

static int hexDigit(unsigned char digit) {
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    digit |= 0x20;
    return digit >= 'a' && digit <= 'f' ? digit - 'a' + 10 : -1;
}

bool DSPetSetupConfigValid(const DSPetSetupConfig *config) {
    const size_t ssidBytes = boundedLength(config->ssid, sizeof(config->ssid));
    const size_t passBytes = boundedLength(config->password, sizeof(config->password));
    const size_t keyBytes  = boundedLength(config->key, sizeof(config->key));
    if (!ssidBytes || ssidBytes >= sizeof(config->ssid) || passBytes < 8 || passBytes >= sizeof(config->password) ||
        !keyBytes || keyBytes >= sizeof(config->key)) {
        return false;
    }
    for (size_t i = 0; i < passBytes; ++i) {
        const unsigned char c = config->password[i];
        if (c < 0x20 || c > 0x7E || (passBytes == 64 && hexDigit(c) < 0)) {
            return false;
        }
    }
    for (size_t i = 0; i < keyBytes; ++i) {
        const unsigned char c = config->key[i];
        if (c < 0x21 || c > 0x7E) {
            return false;
        }
    }
    return true;
}

static bool decode(const char *start, const char *end, char *out, size_t capacity) {
    while (start < end) {
        unsigned char value = *start++;
        if (value == '%') {
            if (end - start < 2) {
                return false;
            }
            const int upper = hexDigit(start[0]);
            const int lower = hexDigit(start[1]);
            if (upper < 0 || lower < 0) {
                return false;
            }
            value = (unsigned char) (16 * upper + lower);
            start += 2;
        } else if (value == '+') {
            value = ' ';
        }
        if (!value || capacity <= 1) {
            return false;
        }
        *out++ = (char) value;
        --capacity;
    }
    *out = '\0';
    return true;
}

bool DSPetSetupFormParse(const char *body, const char *token, DSPetSetupConfig *config) {
    char formToken[33];
    *config = (DSPetSetupConfig) {0};
    const struct {
        const char *name;
        char *out;
        size_t capacity;
    } fields[] = {
        {"ssid", config->ssid, sizeof(config->ssid)},
        {"password", config->password, sizeof(config->password)},
        {"key", config->key, sizeof(config->key)},
        {"token", formToken, sizeof(formToken)},
    };
    const size_t fieldCount = sizeof(fields) / sizeof(fields[0]);
    unsigned seen = 0;
    while (*body) {
        const char *end = body + strcspn(body, "&");
        const char *equals = memchr(body, '=', (size_t) (end - body));
        if (!equals) {
            return false;
        }
        size_t field = 0;
        for (; field < fieldCount; ++field) {
            const size_t nameBytes = (size_t) (equals - body);
            if (nameBytes == strlen(fields[field].name) && memcmp(body, fields[field].name, nameBytes) == 0) {
                break;
            }
        }
        if (field == fieldCount || (seen & (1u << field)) ||
            !decode(equals + 1, end, fields[field].out, fields[field].capacity)) {
            return false;
        }
        seen |= 1u << field;
        body = *end ? end + 1 : end;
    }
    return seen == (1u << fieldCount) - 1 && strcmp(formToken, token) == 0 && DSPetSetupConfigValid(config);
}
