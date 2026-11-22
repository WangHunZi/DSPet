#include "DSPetConsoleTests.h"

#include <errno.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "DSPetAlignmentTest.h"
#include "DSPetDuplexTest.h"
#include "DSPetLoopbackTest.h"
#include "DSPetPlayback.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#ifdef CONFIG_DSPET_DIRECT_TEST
#include <unistd.h>

#include "DSPetDeepSeek.h"
#include "DSPetDirectTest.h"
#include "DSPetWifi.h"
#include "mbedtls/platform_util.h"
#endif
#ifdef CONFIG_DSPET_LOCAL_VOICE
#include "DSPetLocalVoice.h"
#endif
#ifdef CONFIG_DSPET_AEC
#include "DSPetAecTest.h"
#include "DSPetBargeInQuietTest.h"
#include "DSPetBargeInTest.h"
#endif

typedef enum {
    kTestRunning,
    kTestWaiting,
    kTestPass,
    kTestFail,
} TestState;

typedef struct {
    const char *category;
    const char *action;
    const char *name;
    esp_err_t (*run)(void);
    const char *confirmPrompt;
} TestCase;

static const TestCase kCases[] = {
    {"hardware", "duplex", "audio_duplex", DSPetDuplexTestRun, NULL},
    {"hardware", "alignment", "audio_alignment", DSPetAlignmentTestRun, NULL},
#ifdef CONFIG_DSPET_AEC
    {"hardware", "aec", "audio_aec", DSPetAecTestRun, NULL},
    {"hardware", "barge-in-quiet", "audio_barge_in_quiet", DSPetBargeInQuietTestRun, NULL},
    {"human", "barge-in", "audio_barge_in", DSPetBargeInSpeechTestRun, "若插话成功停播，且回放听清了整句"},
    {"human",
     "barge-in-keyboard",
     "audio_barge_in_keyboard",
     DSPetBargeInKeyboardTestRun,
     "若播报期间确实打过字，且播报自然结束"},
#endif
    {"human", "loopback", "audio_loopback", DSPetLoopbackTestRun, "若听清自己的声音"},
};

#ifdef CONFIG_DSPET_DIRECT_TEST
static const TestCase kWifiScan   = {"hardware", "scan", "network_scan", DSPetDirectTestScan, NULL};
static const TestCase kDirectTest = {"hardware", "direct", "network_direct", DSPetDirectTestRun, NULL};
#endif

typedef struct {
    uint32_t        id;
    const TestCase *test;
} TestRun;

static QueueHandle_t sRequests;
static atomic_int    sState = kTestPass;
// 仅串口任务写入。测试任务从队列拿本轮副本。
static TestRun sCurrent;

static const char *stateName(TestState state) {
    switch (state) {
        case kTestRunning: return "running";
        case kTestWaiting: return "waiting_human";
        case kTestPass:    return "pass";
        case kTestFail:    return "fail";
    }
    return "unknown";
}

static void printStatus(TestRun run, TestState state, esp_err_t error) {
    printf("DSPET_TEST id=%" PRIu32 " case=%s state=%s error=%s\n",
           run.id,
           run.test->name,
           stateName(state),
           esp_err_to_name(error));
}

static void testTask(void *context) {
    for (;;) {
        TestRun run;
        if (xQueueReceive(sRequests, &run, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        esp_err_t result = run.test->run();
        TestState state  = result == ESP_OK ? (run.test->confirmPrompt ? kTestWaiting : kTestPass) : kTestFail;
        atomic_store(&sState, state);
        if (state == kTestWaiting) {
            printf("%s，输入 dspet test confirm %" PRIu32 " pass，否则用 fail。\n", run.test->confirmPrompt, run.id);
        }
        printStatus(run, state, result);
    }
}

esp_err_t DSPetConsoleTestsInit(void) {
    sRequests = xQueueCreate(1, sizeof(TestRun));
    if (!sRequests) {
        return ESP_ERR_NO_MEM;
    }
    TaskHandle_t task;
    // 主应用在 CPU0 绘制动画，音频诊断的批量运算在另一核执行。
    const BaseType_t testCore = CONFIG_FREERTOS_NUMBER_OF_CORES > 1 ? 1 : 0;
#ifdef CONFIG_DSPET_DIRECT_TEST
    const unsigned stackBytes = 8192;
#else
    const unsigned stackBytes = 4096;
#endif
    if (xTaskCreatePinnedToCore(testTask, "dspet-test", stackBytes, NULL, 3, &task, testCore) != pdPASS) {
        vQueueDelete(sRequests);
        sRequests = NULL;
        return ESP_ERR_NO_MEM;
    }
#ifndef CONFIG_DSPET_LOCAL_VOICE
    esp_err_t error = DSPetPlaybackInit();
    if (error != ESP_OK) {
        vTaskDelete(task);
        vQueueDelete(sRequests);
        sRequests = NULL;
    }
    return error;
#else
    return ESP_OK;
#endif
}

bool DSPetConsoleTestsAudioBusy(void) { return atomic_load(&sState) == kTestRunning; }

void DSPetConsoleTestsHelp(void) {
    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i) {
        printf("dspet test %s audio %s\n", kCases[i].category, kCases[i].action);
    }
    puts("dspet test status");
    puts("dspet test confirm <run-id> pass|fail");
#ifdef CONFIG_DSPET_DIRECT_TEST
    puts("dspet test hardware network scan <SSID-prefix>");
    puts("dspet test hardware network direct <SSID>");
#endif
}

static bool testAvailable(const TestCase *test) {
#ifdef CONFIG_DSPET_LOCAL_VOICE
    bool needsAudio = true;
#ifdef CONFIG_DSPET_DIRECT_TEST
    needsAudio = test != &kWifiScan;
#endif
    if (needsAudio && DSPetLocalVoiceGetMode() != kDSPetLocalVoiceOff) {
        puts("DSPET_ERROR voice_owns_audio; run dspet voice disable");
        return false;
    }
#endif
    TestState state = atomic_load(&sState);
    if (!sRequests || state == kTestRunning || state == kTestWaiting) {
        puts("DSPET_ERROR test_runner_busy_or_unavailable");
        return false;
    }
    return true;
}

static int startTest(const TestCase *test) {
    if (!testAvailable(test)) {
        return 1;
    }
    sCurrent = (TestRun) {.id = sCurrent.id + 1, .test = test};
    atomic_store(&sState, kTestRunning);
    printStatus(sCurrent, kTestRunning, ESP_OK);
    if (xQueueSend(sRequests, &sCurrent, 0) != pdTRUE) {
        atomic_store(&sState, kTestFail);
        printStatus(sCurrent, kTestFail, ESP_ERR_INVALID_STATE);
        return 1;
    }
    return 0;
}

#ifdef CONFIG_DSPET_DIRECT_TEST
static bool readSecret(const char *field, char *value, size_t capacity) {
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

static int prepareNetwork(const TestCase *test, const char *ssid) {
    if (!testAvailable(test)) {
        return 1;
    }
    const size_t ssidBytes = strlen(ssid);
    if (!ssidBytes || ssidBytes > kDSPetWifiSsidBytes) {
        puts("DSPET_ERROR invalid_ssid");
        return 1;
    }
    esp_err_t error = ESP_ERR_INVALID_ARG;
    if (test == &kWifiScan) {
        error = DSPetDirectTestPrepare(ssid, NULL, NULL);
    } else {
        char password[kDSPetWifiPasswordBytes + 1];
        char key[kDSPetDeepSeekKeyBytes + 1];
        if (readSecret("wifi_password", password, sizeof(password)) && readSecret("deepseek_key", key, sizeof(key))) {
            error = DSPetDirectTestPrepare(ssid, password, key);
        }
        mbedtls_platform_zeroize(password, sizeof(password));
        mbedtls_platform_zeroize(key, sizeof(key));
    }
    if (error != ESP_OK) {
        puts("DSPET_ERROR invalid_network_input");
        return 1;
    }
    const int result = startTest(test);
    if (result) {
        DSPetDirectTestClear();
    }
    return result;
}
#endif

static int confirm(const char *idArg, const char *verdict) {
    char *end;
    errno          = 0;
    uintmax_t id   = strtoumax(idArg, &end, 10);
    bool      pass = strcmp(verdict, "pass") == 0;
    if (errno || end == idArg || *end || idArg[0] < '0' || idArg[0] > '9' || id > UINT32_MAX ||
        (!pass && strcmp(verdict, "fail") != 0)) {
        puts("DSPET_ERROR invalid_confirmation");
        return 1;
    }
    if (atomic_load(&sState) != kTestWaiting || id != sCurrent.id) {
        puts("DSPET_ERROR run_not_waiting_for_this_confirmation");
        return 1;
    }
    TestState state = pass ? kTestPass : kTestFail;
    printStatus(sCurrent, state, pass ? ESP_OK : ESP_FAIL);
    atomic_store(&sState, state);
    return 0;
}

int DSPetConsoleTestsRunCommand(int argc, char **argv) {
#ifdef CONFIG_DSPET_DIRECT_TEST
    if (argc == 4 && strcmp(argv[0], "hardware") == 0 && strcmp(argv[1], "network") == 0) {
        if (strcmp(argv[2], "scan") == 0) {
            return prepareNetwork(&kWifiScan, argv[3]);
        }
        if (strcmp(argv[2], "direct") == 0) {
            return prepareNetwork(&kDirectTest, argv[3]);
        }
    }
#endif
    if (argc == 3 && strcmp(argv[1], "audio") == 0) {
        for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i) {
            if (strcmp(argv[0], kCases[i].category) == 0 && strcmp(argv[2], kCases[i].action) == 0) {
                return startTest(&kCases[i]);
            }
        }
    }
    if (argc == 3 && strcmp(argv[0], "confirm") == 0) {
        return confirm(argv[1], argv[2]);
    }
    if (argc == 1 && strcmp(argv[0], "status") == 0) {
        if (!sCurrent.id) {
            puts("DSPET_TEST state=idle");
        } else {
            printf("DSPET_TEST_STATUS id=%" PRIu32 " state=%s\n", sCurrent.id, stateName(atomic_load(&sState)));
        }
        return 0;
    }
    DSPetConsoleTestsHelp();
    return argc == 0 ? 0 : 1;
}
