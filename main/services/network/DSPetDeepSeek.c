#include "DSPetDeepSeek.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "DSPetPcmWav.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "mbedtls/platform_util.h"

enum {
    kHttpTimeoutMs      = 12000,
    kUploadTimeoutMs    = 35000,
    kAsrHeaderTimeoutMs = 24000,
    kReadPollMs         = 250,
    kReplyJsonBytes     = 24576,
    kReadBlockBytes     = 768
};
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

typedef struct {
    const void *data;
    size_t      bytes;
} RequestPart;

typedef struct {
    const char        *url;
    const char        *stage;
    const char        *contentType;
    const RequestPart *parts;
    size_t             partCount;
    bool               expectPcm;
} Request;

static bool cancelled(const DSPetDeepSeekCancel *cancel) { return cancel && cancel->requested(cancel->context); }

static esp_err_t postRequestOnce(const char                *key,
                                 const Request             *request,
                                 DSPetDeepSeekAudioSink     sink,
                                 void                      *context,
                                 const DSPetDeepSeekCancel *cancel) {
    if (cancelled(cancel)) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t uploadBytes = 0;
    for (size_t i = 0; i < request->partCount; ++i) {
        uploadBytes += request->parts[i].bytes;
    }
    const char              *stage   = request->stage;
    ResponseHeaders          headers = {0};
    esp_http_client_config_t config  = {
        .url                   = request->url,
        .method                = HTTP_METHOD_POST,
        .crt_bundle_attach     = esp_crt_bundle_attach,
        .timeout_ms            = kHttpTimeoutMs,
        .disable_auto_redirect = true,
        .event_handler         = saveContentType,
        .user_data             = &headers,
    };
    esp_http_client_handle_t http = esp_http_client_init(&config);
    if (!http) {
        return ESP_ERR_NO_MEM;
    }
    char authorization[sizeof("Bearer ") + kDSPetDeepSeekKeyBytes];
    snprintf(authorization, sizeof(authorization), "Bearer %s", key);
    esp_err_t error = esp_http_client_set_header(http, "Authorization", authorization);
    mbedtls_platform_zeroize(authorization, sizeof(authorization));
    if (error == ESP_OK) {
        error = esp_http_client_set_header(http, "Content-Type", request->contentType);
    }
    if (error == ESP_OK) {
        error = esp_http_client_set_header(http, "Connection", "close");
    }
    const int64_t startedUs = esp_timer_get_time();
    if (error == ESP_OK) {
        error = esp_http_client_open(http, uploadBytes);
        printf("DSPET_DIRECT_TLS stage=%s result=%s elapsed_us=%" PRId64 " free_internal=%zu largest_internal=%zu\n",
               stage,
               esp_err_to_name(error),
               esp_timer_get_time() - startedUs,
               heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
               heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    size_t written = 0;
    for (size_t i = 0; error == ESP_OK && i < request->partCount; ++i) {
        const RequestPart *part = &request->parts[i];
        for (size_t sent = 0; sent < part->bytes;) {
            if (cancelled(cancel)) {
                error = ESP_ERR_INVALID_STATE;
                break;
            }
            if (esp_timer_get_time() - startedUs >= kUploadTimeoutMs * INT64_C(1000)) {
                error = ESP_ERR_TIMEOUT;
                break;
            }
            size_t bytes = part->bytes - sent;
            if (bytes > 4096) {
                bytes = 4096;
            }
            int count = esp_http_client_write(http, (const char *) part->data + sent, bytes);
            if (count <= 0) {
                error = ESP_FAIL;
                break;
            }
            sent += (size_t) count;
            written += (size_t) count;
        }
    }
    if (request->partCount > 1) {
        printf("DSPET_DIRECT_UPLOAD stage=%s bytes=%zu total=%zu result=%s elapsed_us=%" PRId64 "\n",
               stage,
               written,
               uploadBytes,
               esp_err_to_name(error),
               esp_timer_get_time() - startedUs);
    }
    if (error == ESP_OK && request->partCount > 1) {
        error = esp_http_client_set_timeout_ms(http, kAsrHeaderTimeoutMs);
    }
    if (error == ESP_OK && esp_http_client_fetch_headers(http) < 0) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error == ESP_OK) {
        const int status = esp_http_client_get_status_code(http);
        printf("DSPET_DIRECT_HTTP stage=%s status=%d headers_us=%" PRId64 "\n",
               stage,
               status,
               esp_timer_get_time() - startedUs);
        if (status != 200 || (request->expectPcm && !headers.pcm)) {
            error = ESP_ERR_INVALID_RESPONSE;
        }
    }

    if (error == ESP_OK && cancel) {
        error = esp_http_client_set_timeout_ms(http, kReadPollMs);
    }
    int64_t lastDataUs = esp_timer_get_time();
    size_t  bytes      = 0;
    uint8_t block[kReadBlockBytes];
    while (error == ESP_OK) {
        if (cancelled(cancel)) {
            error = ESP_ERR_INVALID_STATE;
            break;
        }
        const int count = esp_http_client_read(http, (char *) block, sizeof(block));
        if (count == -ESP_ERR_HTTP_EAGAIN && esp_timer_get_time() - lastDataUs < kHttpTimeoutMs * INT64_C(1000)) {
            continue;
        }
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
        error      = cancelled(cancel) ? ESP_ERR_INVALID_STATE : sink(block, (size_t) count, context);
        lastDataUs = esp_timer_get_time();
    }
    printf("DSPET_DIRECT_TRANSFER stage=%s bytes=%zu result=%s elapsed_us=%" PRId64 "\n",
           stage,
           bytes,
           esp_err_to_name(error),
           esp_timer_get_time() - startedUs);
    esp_http_client_cleanup(http);
    return error;
}

static esp_err_t postRequest(const char                *key,
                             const Request             *request,
                             DSPetDeepSeekAudioSink     sink,
                             void                      *context,
                             const DSPetDeepSeekCancel *cancel) {
    esp_err_t error = postRequestOnce(key, request, sink, context, cancel);
    if (cancel && error == ESP_ERR_HTTP_CONNECT && !cancelled(cancel)) {
        // 连接尚未打开，尚未写入音频或文本；弱链路下仅尝试重建一次。
        printf("DSPET_DIRECT_RETRY stage=%s phase=connect\n", request->stage);
        error = postRequestOnce(key, request, sink, context, cancel);
    }
    return error;
}

static esp_err_t postJson(const char                *key,
                          const char                *url,
                          const char                *stage,
                          const cJSON               *body,
                          bool                       expectPcm,
                          DSPetDeepSeekAudioSink     sink,
                          void                      *context,
                          const DSPetDeepSeekCancel *cancel) {
    char *json = cJSON_PrintUnformatted(body);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    const RequestPart part    = {json, strlen(json)};
    const Request     request = {url, stage, "application/json", &part, 1, expectPcm};
    esp_err_t         error   = postRequest(key, &request, sink, context, cancel);
    cJSON_free(json);
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

static const char kAddressingPrompt[] =
    "当前是桌宠的免唤醒模式。最后一条 user 消息是麦克风拾到的语音转写，默认保持静默。"
    "语句有明确的问题、请求、对桌宠的招呼，或承接最近问答的续聊时才回应，无需喊名字。"
    "自言自语、口述备忘、对旁人说话、媒体声音都静默。没有问答上下文时，"
    "单独的陈述、应声和没说完的片段也静默，不要替片段补出问题或主动追问。"
    "例如「一加一等于几」是提问，「我知道了，现在这个画面」是片段。"
    "按 JSON 格式输出。回应时输出 {\"action\":\"reply\",\"reply\":\"你想说的话\"}，"
    "reply 里只放自然的朗读正文。静默时输出 {\"action\":\"ignore\"}。";

static esp_err_t copyReply(const char *content, char *reply, size_t capacityBytes) {
    if (!content || strspn(content, " \t\r\n") == strlen(content)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    const size_t count = strlen(content) + 1;
    if (count > capacityBytes) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(reply, content, count);
    return ESP_OK;
}

static esp_err_t parseAddressedReply(const char *content, char *reply, size_t capacityBytes) {
    cJSON      *decision = cJSON_ParseWithOpts(content, NULL, true);
    const char *action   = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(decision, "action"));
    esp_err_t   error    = ESP_ERR_INVALID_RESPONSE;
    if (action && strcmp(action, "reply") == 0) {
        error =
            copyReply(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(decision, "reply")), reply, capacityBytes);
    } else if (action && strcmp(action, "ignore") == 0) {
        error = ESP_ERR_NOT_FOUND;
    }
    cJSON_Delete(decision);
    return error;
}

static esp_err_t parseReply(JsonResponse *response, bool addressedOnly, char *reply, size_t capacityBytes) {
    response->data[response->size] = '\0';
    cJSON      *data               = cJSON_ParseWithLength(response->data, response->size + 1);
    cJSON      *choices            = cJSON_GetObjectItemCaseSensitive(data, "choices");
    cJSON      *choice             = cJSON_GetArrayItem(choices, 0);
    const char *finished           = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(choice, "finish_reason"));
    cJSON      *message            = cJSON_GetObjectItemCaseSensitive(choice, "message");
    const char *content            = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(message, "content"));
    esp_err_t   error              = ESP_ERR_INVALID_RESPONSE;
    if (finished && strcmp(finished, "stop") == 0 && content) {
        error = addressedOnly ? parseAddressedReply(content, reply, capacityBytes)
                              : copyReply(content, reply, capacityBytes);
    }
    cJSON_Delete(data);
    return error;
}

esp_err_t DSPetDeepSeekTranscribe(const char                *key,
                                  const int16_t             *samples,
                                  size_t                     sampleCount,
                                  uint32_t                   sampleRateHz,
                                  char                      *transcript,
                                  size_t                     capacityBytes,
                                  const DSPetDeepSeekCancel *cancel) {
    if ((sampleRateHz != 8000 && sampleRateHz != 16000) || !sampleCount ||
        sampleCount > (8 * 1024 * 1024 - kDSPetPcmWavHeaderBytes) / sizeof(*samples)) {
        return ESP_ERR_INVALID_SIZE;
    }
    static const char lead[] = "--dspet-pcm16-wav-esp32-s3\r\n"
                               "Content-Disposition: form-data; name=\"file\"; filename=\"speech.wav\"\r\n"
                               "Content-Type: audio/wav\r\n\r\n";
    static const char tail[] = "\r\n--dspet-pcm16-wav-esp32-s3--\r\n";
    uint8_t           wav[kDSPetPcmWavHeaderBytes];
    DSPetPcmWavHeader(wav, sampleRateHz, sampleCount);
    const RequestPart parts[] = {
        {lead, sizeof(lead) - 1},
        {wav, sizeof(wav)},
        {samples, sampleCount * sizeof(*samples)},
        {tail, sizeof(tail) - 1},
    };
    const Request request  = {"https://api.deepseek.com/v1/tools/asr",
                              "asr",
                              "multipart/form-data; boundary=dspet-pcm16-wav-esp32-s3",
                              parts,
                              sizeof(parts) / sizeof(parts[0]),
                              false};
    JsonResponse  response = {.data = heap_caps_malloc(kReplyJsonBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)};
    if (!response.data) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t error = postRequest(key, &request, appendJson, &response, cancel);
    if (error == ESP_OK) {
        response.data[response.size] = '\0';
        cJSON      *data             = cJSON_ParseWithLength(response.data, response.size + 1);
        const char *text             = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(data, "text"));
        if (!text) {
            error = ESP_ERR_INVALID_RESPONSE;
        } else if (!*text || strspn(text, " \t\r\n") == strlen(text)) {
            error = ESP_ERR_NOT_FOUND;
        } else if (strlen(text) >= capacityBytes) {
            error = ESP_ERR_INVALID_SIZE;
        } else {
            memcpy(transcript, text, strlen(text) + 1);
        }
        cJSON_Delete(data);
    }
    free(response.data);
    return error;
}

static bool addHistory(cJSON *messages, const DSPetDeepSeekHistory *history, bool addressedOnly) {
    if (!history) {
        return true;
    }
    const char *reply           = history->reply;
    char       *structuredReply = NULL;
    if (addressedOnly) {
        // 保持历史 assistant 消息与当前回合的 JSON 输出格式一致。
        cJSON *decision = cJSON_CreateObject();
        if (cJSON_AddStringToObject(decision, "action", "reply") && cJSON_AddStringToObject(decision, "reply", reply)) {
            structuredReply = cJSON_PrintUnformatted(decision);
        }
        cJSON_Delete(decision);
        if (!structuredReply) {
            return false;
        }
        reply = structuredReply;
    }
    const bool added = addMessage(messages, "user", history->question) && addMessage(messages, "assistant", reply) &&
                       (!history->interrupted ||
                        addMessage(messages, "system", "上一条回复的播报被打断或中途停止，用户可能只听到部分内容。"));
    cJSON_free(structuredReply);
    return added;
}

esp_err_t DSPetDeepSeekReply(const char                 *key,
                             const char                 *question,
                             const DSPetDeepSeekHistory *history,
                             bool                        addressedOnly,
                             char                       *reply,
                             size_t                      capacityBytes,
                             const DSPetDeepSeekCancel  *cancel) {
    cJSON *body = cJSON_CreateObject();
    if (!body) {
        return ESP_ERR_NO_MEM;
    }
    cJSON       *messages = cJSON_AddArrayToObject(body, "messages");
    JsonResponse response = {0};
    esp_err_t    error    = ESP_ERR_NO_MEM;
    if (messages && cJSON_AddStringToObject(body, "model", "deepseek-flash") &&
        cJSON_AddStringToObject(body, "reasoning_effort", "low") && cJSON_AddNumberToObject(body, "max_tokens", 512) &&
        addMessage(messages, "system", kSystemPrompt) &&
        (!addressedOnly ||
         (cJSON_AddStringToObject(cJSON_AddObjectToObject(body, "response_format"), "type", "json_object") &&
          addMessage(messages, "system", kAddressingPrompt))) &&
        addHistory(messages, history, addressedOnly) && addMessage(messages, "user", question)) {
        response.data = heap_caps_malloc(kReplyJsonBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (response.data) {
            error = postJson(
                key, "https://api.deepseek.com/chat/completions", "chat", body, false, appendJson, &response, cancel);
            if (error == ESP_OK) {
                error = parseReply(&response, addressedOnly, reply, capacityBytes);
            }
        }
    }
    free(response.data);
    cJSON_Delete(body);
    return error;
}

esp_err_t DSPetDeepSeekSpeak(
    const char *key, const char *text, DSPetDeepSeekAudioSink sink, void *context, const DSPetDeepSeekCancel *cancel) {
    cJSON *body = cJSON_CreateObject();
    if (!body) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t error = ESP_ERR_NO_MEM;
    if (cJSON_AddStringToObject(body, "text", text) && cJSON_AddStringToObject(body, "voice_id", "stella") &&
        cJSON_AddStringToObject(body, "format", "pcm")) {
        error = postJson(key, "https://api.deepseek.com/v1/tools/tts", "tts", body, true, sink, context, cancel);
    }
    cJSON_Delete(body);
    return error;
}
