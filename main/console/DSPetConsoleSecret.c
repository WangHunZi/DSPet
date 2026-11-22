#include "DSPetConsoleSecret.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>

bool DSPetConsoleReadSecret(const char *field, char *value, size_t capacity) {
    printf("DSPET_INPUT field=%s\n", field);
    fflush(stdout);
    size_t        used  = 0;
    bool          valid = true;
    unsigned char byte;
    // USB Serial/JTAG 的 VFS 不回显。绕过会将输入写回终端的 linenoise。
    while (read(fileno(stdin), &byte, 1) == 1) {
        if (byte == '\n' || byte == '\r') {
            value[used] = '\0';
            return valid && used;
        }
        if (byte == 3) {
            return false;
        }
        if ((byte == 8 || byte == 127) && used) {
            value[--used] = '\0';
            continue;
        }
        if (byte >= 32 && byte != 127 && used + 1 < capacity) {
            value[used++] = (char) byte;
        } else {
            valid = false;
        }
    }
    return false;
}

bool DSPetConsoleParseUnixTime(const char *text, uint32_t *unixTime) {
    if (text[0] < '0' || text[0] > '9') {
        return false;
    }
    char *end;
    errno                 = 0;
    const uintmax_t value = strtoumax(text, &end, 10);
    if (errno || *end || value < 1600000000 || value > UINT32_MAX) {
        return false;
    }
    *unixTime = (uint32_t) value;
    return true;
}
