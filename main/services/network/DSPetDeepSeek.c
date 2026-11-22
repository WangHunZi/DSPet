#include "DSPetDeepSeek.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "mbedtls/platform_util.h"

enum { kHttpTimeoutMs = 12000, kReplyJsonBytes = 24576, kReadBlockBytes = 768 };
extern const char kSystemPrompt[] asm("_binary_system_prompt_txt_start");

typedef struct {
    bool pcm;
} ResponseHeaders;

static esp_err_t saveContentType(esp_http_client_event_t *event) {
    if (event->event_id == HTTP_EVENT_ON_HEADER && strcasecmp(event->header_key, "Content-Type") == 0) {
        ResponseHeaders *headers = event->user_data;
        const char      *mime    = event->header_value;
        headers->pcm             = strncasecmp(mime, "audio/pcm", 9) == 0 && (mime[9] == '\0' || mime[9] == ';');
    }
    return ESP_OK;
}

static esp_err_t postJson(const char            *key,
                          const char            *url,
                          const char            *stage,
                          const cJSON           *body,
                          bool                   expectPcm,
                          DSPetDeepSeekAudioSink sink,
                          void                  *context) {
    char *json = cJSON_PrintUnformatted(body);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    ResponseHeaders          headers = {0};
    esp_http_client_config_t config  = {
        .url                   = url,
        .method                = HTTP_METHOD_POST,
        .crt_bundle_attach     = esp_crt_bundle_attach,
        .timeout_ms            = kHttpTimeoutMs,
        .disable_auto_redirect = true,
        .event_handler         = saveContentType,
        .user_data             = &headers,
    };
    esp_http_client_handle_t http = esp_http_client_init(&config);
    if (!http) {
        cJSON_free(json);
        return ESP_ERR_NO_MEM;
    }
    char authorization[sizeof("Bearer ") + kDSPetDeepSeekKeyBytes];
    snprintf(authorization, sizeof(authorization), "Bearer %s", key);
    esp_err_t error = esp_http_client_set_header(http, "Authorization", authorization);
    mbedtls_platform_zeroize(authorization, sizeof(authorization));
    if (error == ESP_OK) {
        error = esp_http_client_set_header(http, "Content-Type", "application/json");
    }
    const int64_t startedUs = esp_timer_get_time();
    if (error == ESP_OK) {
        error = esp_http_client_open(http, strlen(json));
        printf("DSPET_DIRECT_TLS stage=%s result=%s elapsed_us=%" PRId64 " free_internal=%zu largest_internal=%zu\n",
               stage,
               esp_err_to_name(error),
               esp_timer_get_time() - startedUs,
               heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
               heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (error == ESP_OK && esp_http_client_write(http, json, strlen(json)) != (int) strlen(json)) {
        error = ESP_FAIL;
    }
    cJSON_free(json);
    if (error == ESP_OK && esp_http_client_fetch_headers(http) < 0) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error == ESP_OK) {
        const int status = esp_http_client_get_status_code(http);
        printf("DSPET_DIRECT_HTTP stage=%s status=%d headers_us=%" PRId64 "\n",
               stage,
               status,
               esp_timer_get_time() - startedUs);
        if (status != 200 || (expectPcm && !headers.pcm)) {
            error = ESP_ERR_INVALID_RESPONSE;
        }
    }

    size_t  bytes = 0;
    uint8_t block[kReadBlockBytes];
    while (error == ESP_OK) {
        const int count = esp_http_client_read(http, (char *) block, sizeof(block));
        if (count <= 0) {
            if (count < 0 || !bytes || !esp_http_client_is_complete_data_received(http)) {
                error = ESP_ERR_INVALID_RESPONSE;
            }
            break;
        }
        if (bytes == 0) {
            printf(
                "DSPET_DIRECT_FIRST_BYTE stage=%s elapsed_us=%" PRId64 "\n", stage, esp_timer_get_time() - startedUs);
        }
        bytes += (size_t) count;
        error = sink(block, (size_t) count, context);
    }
    printf("DSPET_DIRECT_TRANSFER stage=%s bytes=%zu result=%s elapsed_us=%" PRId64 "\n",
           stage,
           bytes,
           esp_err_to_name(error),
           esp_timer_get_time() - startedUs);
    esp_http_client_cleanup(http);
    return error;
}

typedef struct {
    char  *data;
    size_t size;
} JsonResponse;

static esp_err_t appendJson(const uint8_t *bytes, size_t count, void *context) {
    JsonResponse *response = context;
    if (count >= kReplyJsonBytes - response->size) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(response->data + response->size, bytes, count);
    response->size += count;
    return ESP_OK;
}

static bool addMessage(cJSON *messages, const char *role, const char *content) {
    cJSON *message = cJSON_CreateObject();
    if (!message) {
        return false;
    }
    if (!cJSON_AddStringToObject(message, "role", role) || !cJSON_AddStringToObject(message, "content", content) ||
        !cJSON_AddItemToArray(messages, message)) {
        cJSON_Delete(message);
        return false;
    }
    return true;
}

static esp_err_t parseReply(JsonResponse *response, char *reply, size_t capacityBytes) {
    response->data[response->size] = '\0';
    cJSON      *data               = cJSON_ParseWithLength(response->data, response->size + 1);
    cJSON      *choices            = cJSON_GetObjectItemCaseSensitive(data, "choices");
    cJSON      *choice             = cJSON_GetArrayItem(choices, 0);
    const char *finished           = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(choice, "finish_reason"));
    cJSON      *message            = cJSON_GetObjectItemCaseSensitive(choice, "message");
    const char *content            = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(message, "content"));
    esp_err_t   error              = ESP_ERR_INVALID_RESPONSE;
    if (finished && strcmp(finished, "stop") == 0 && content && *content) {
        const size_t count = strlen(content) + 1;
        if (count <= capacityBytes) {
            memcpy(reply, content, count);
            error = ESP_OK;
        } else {
            error = ESP_ERR_INVALID_SIZE;
        }
    }
    cJSON_Delete(data);
    return error;
}

esp_err_t DSPetDeepSeekReply(const char *key, const char *question, char *reply, size_t capacityBytes) {
    cJSON *body = cJSON_CreateObject();
    if (!body) {
        return ESP_ERR_NO_MEM;
    }
    cJSON       *messages = cJSON_AddArrayToObject(body, "messages");
    JsonResponse response = {0};
    esp_err_t    error    = ESP_ERR_NO_MEM;
    if (messages && cJSON_AddStringToObject(body, "model", "deepseek-flash") &&
        cJSON_AddStringToObject(body, "reasoning_effort", "low") && cJSON_AddNumberToObject(body, "max_tokens", 512) &&
        addMessage(messages, "system", kSystemPrompt) && addMessage(messages, "user", question)) {
        response.data = heap_caps_malloc(kReplyJsonBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (response.data) {
            error =
                postJson(key, "https://api.deepseek.com/chat/completions", "chat", body, false, appendJson, &response);
            if (error == ESP_OK) {
                error = parseReply(&response, reply, capacityBytes);
            }
        }
    }
    free(response.data);
    cJSON_Delete(body);
    return error;
}

esp_err_t DSPetDeepSeekSpeak(const char *key, const char *text, DSPetDeepSeekAudioSink sink, void *context) {
    cJSON *body = cJSON_CreateObject();
    if (!body) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t error = ESP_ERR_NO_MEM;
    if (cJSON_AddStringToObject(body, "text", text) && cJSON_AddStringToObject(body, "voice_id", "stella") &&
        cJSON_AddStringToObject(body, "format", "pcm")) {
        error = postJson(key, "https://api.deepseek.com/v1/tools/tts", "tts", body, true, sink, context);
    }
    cJSON_Delete(body);
    return error;
}
