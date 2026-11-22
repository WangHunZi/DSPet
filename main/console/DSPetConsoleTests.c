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
    bool human;
} TestCase;

static const TestCase kCases[] = {
    {"hardware", "duplex", "audio_duplex", DSPetDuplexTestRun, false},
    {"hardware", "alignment", "audio_alignment", DSPetAlignmentTestRun, false},
    {"human", "loopback", "audio_loopback", DSPetLoopbackTestRun, true},
};

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
        TestState state  = result == ESP_OK ? (run.test->human ? kTestWaiting : kTestPass) : kTestFail;
        atomic_store(&sState, state);
        if (state == kTestWaiting) {
            printf("如果听清自己的声音，输入 dspet test confirm %" PRIu32 " pass，否则用 fail。\n", run.id);
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
    if (xTaskCreate(testTask, "dspet-test", 4096, NULL, 3, &task) != pdPASS) {
        vQueueDelete(sRequests);
        sRequests = NULL;
        return ESP_ERR_NO_MEM;
    }
    esp_err_t error = DSPetPlaybackInit();
    if (error != ESP_OK) {
        vTaskDelete(task);
        vQueueDelete(sRequests);
        sRequests = NULL;
    }
    return error;
}

void DSPetConsoleTestsHelp(void) {
    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i) {
        printf("dspet test %s audio %s\n", kCases[i].category, kCases[i].action);
    }
    puts("dspet test status");
    puts("dspet test confirm <run-id> pass|fail");
}

static int startTest(const TestCase *test) {
    TestState state = atomic_load(&sState);
    if (!sRequests || state == kTestRunning || state == kTestWaiting) {
        puts("DSPET_ERROR test_runner_busy_or_unavailable");
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
