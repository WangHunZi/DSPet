#include "DSPetConsole.h"

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_console.h"
#include "sdkconfig.h"
#ifdef CONFIG_DSPET_CONSOLE_TESTS
#include "DSPetConsoleTests.h"
#endif
#include <stdio.h>
#include <string.h>

static const char *const kTag = "console";

static void printHelp(void) {
    puts("dspet help");
    puts("dspet info");
#ifdef CONFIG_DSPET_CONSOLE_TESTS
    DSPetConsoleTestsHelp();
#endif
}

static int handleCommand(int argc, char **argv) {
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "help") == 0)) {
        printHelp();
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "info") == 0) {
        const esp_app_desc_t *app = esp_app_get_description();
        printf("DSPET_INFO project=%s version=%s idf=%s\n", app->project_name, app->version, app->idf_ver);
        return 0;
    }
#ifdef CONFIG_DSPET_CONSOLE_TESTS
    if (strcmp(argv[1], "test") == 0) {
        return DSPetConsoleTestsRunCommand(argc - 2, argv + 2);
    }
#endif
    puts("Unknown command or arguments. Run dspet help.");
    return 1;
}

esp_err_t DSPetConsoleStart(void) {
    esp_console_repl_config_t replConfig               = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    replConfig.prompt                                  = "xiao>";
    esp_console_dev_usb_serial_jtag_config_t usbConfig = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_console_repl_t                      *repl      = NULL;
    ESP_RETURN_ON_ERROR(
        esp_console_new_repl_usb_serial_jtag(&usbConfig, &replConfig, &repl), kTag, "create serial REPL");
    const esp_console_cmd_t command = {
        .command = "dspet",
        .help    = "DSPet device commands",
        .func    = handleCommand,
    };
    esp_err_t error = esp_console_cmd_register(&command);
#ifdef CONFIG_DSPET_CONSOLE_TESTS
    if (error == ESP_OK) {
        error = DSPetConsoleTestsInit();
    }
#endif
    if (error == ESP_OK) {
        error = esp_console_start_repl(repl);
    }
    if (error != ESP_OK) {
        repl->del(repl);
    }
    return error;
}
