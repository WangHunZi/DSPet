#include "DSPetSetupPortal.h"

#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "DSPetSetupStore.h"
#include "DSPetWifi.h"
#include "bootloader_random.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include "lwip/sockets.h"
#include "mbedtls/platform_util.h"

enum { kMaxFormBytes = 800, kDnsHeaderBytes = 12, kDnsPort = 53 };
static const char     kPortalUrl[] = "http://" DSPET_SETUP_PORTAL_ADDRESS "/";
static httpd_handle_t sServer;
static int            sDnsSocket = -1;
static atomic_bool    sSaved;
static char           sToken[33];

static void hexEncode(char *out, const uint8_t *bytes, size_t count) {
    const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < count; ++i) {
        *out++ = hex[bytes[i] >> 4];
        *out++ = hex[bytes[i] & 15];
    }
    *out = '\0';
}

static const char kFormHead[] =
    "<!doctype html><html lang=zh-CN><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>DSPet 配网</title><style>"
    "body{font:1rem system-ui;margin:2rem auto;max-width:25rem;padding:0 1rem;color:#142d41;background:#ecf6fb}"
    "form{display:grid;gap:1rem}label{display:grid;gap:.3rem}input,button{font:inherit;padding:.7rem;border-radius:."
    "5rem;"
    "border:1px solid #52718b}button{background:#145078;color:#fff}</style>"
    "<h1>DSPet 配网</h1><form method=post action=/setup>"
    "<label>Wi-Fi 名称（2.4 GHz）<input name=ssid maxlength=32 autocomplete=off required></label>"
    "<label>Wi-Fi 密码<input type=password name=password minlength=8 maxlength=64 autocomplete=off required></label>"
    "<label>DeepSeek API Key<input type=password name=key maxlength=128 autocomplete=off required></label>"
    "<input type=hidden name=token value='";
static const char kFormTail[] = "'><button>保存并联网</button></form></html>";

static bool portalHost(httpd_req_t *request) {
    char host[32];
    return httpd_req_get_hdr_value_str(request, "Host", host, sizeof(host)) == ESP_OK &&
           (strcmp(host, DSPET_SETUP_PORTAL_ADDRESS) == 0 || strcmp(host, DSPET_SETUP_PORTAL_ADDRESS ":80") == 0);
}

static esp_err_t sendRedirect(httpd_req_t *request) {
    httpd_resp_set_status(request, "303 See Other");
    httpd_resp_set_hdr(request, "Location", kPortalUrl);
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, "Open the DSPet setup page");
}

static esp_err_t showForm(httpd_req_t *request) {
    if (!portalHost(request) || strcmp(request->uri, "/") != 0) {
        return sendRedirect(request);
    }
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
    esp_err_t error = httpd_resp_sendstr_chunk(request, kFormHead);
    if (error == ESP_OK) {
        error = httpd_resp_sendstr_chunk(request, sToken);
    }
    if (error == ESP_OK) {
        error = httpd_resp_sendstr_chunk(request, kFormTail);
    }
    return error == ESP_OK ? httpd_resp_sendstr_chunk(request, NULL) : error;
}

static esp_err_t saveForm(httpd_req_t *request) {
    if (!portalHost(request) || atomic_load(&sSaved)) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Setup request unavailable");
    }
    const size_t size = request->content_len;
    if (!size || size > kMaxFormBytes) {
        return httpd_resp_send_err(request, HTTPD_413_CONTENT_TOO_LARGE, NULL);
    }
    char body[kMaxFormBytes + 1];
    for (size_t received = 0; received < size;) {
        const int count = httpd_req_recv(request, body + received, size - received);
        if (count <= 0) {
            mbedtls_platform_zeroize(body, sizeof(body));
            return httpd_resp_send_err(request, HTTPD_408_REQ_TIMEOUT, NULL);
        }
        received += (size_t) count;
    }
    body[size]              = '\0';
    DSPetSetupConfig config = {0};
    const bool       parsed = !memchr(body, 0, size) && DSPetSetupFormParse(body, sToken, &config);
    mbedtls_platform_zeroize(body, sizeof(body));
    const esp_err_t error = parsed ? DSPetSetupStoreSave(kDSPetSetupNamespace, &config) : ESP_ERR_INVALID_ARG;
    mbedtls_platform_zeroize(&config, sizeof(config));
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (error != ESP_OK) {
        httpd_resp_set_status(request, parsed ? "500 Internal Server Error" : "400 Bad Request");
        return httpd_resp_sendstr(
            request,
            "<meta charset=utf-8><p>保存失败。请检查 Wi-Fi 名称、密码和 API Key，再重试。</p><a href=/>返回</a>");
    }
    const esp_err_t sent = httpd_resp_sendstr(request, "<meta charset=utf-8><p>已保存。桌宠正在重启并连接 Wi-Fi。</p>");
    // 即使客户端已断开，已提交的配置也应在本次重启生效。
    atomic_store(&sSaved, true);
    return sent;
}

static esp_err_t startHttp(void) {
    httpd_config_t config    = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn      = httpd_uri_match_wildcard;
    config.lru_purge_enable  = true;
    config.recv_wait_timeout = 3;
    esp_err_t error          = httpd_start(&sServer, &config);
    if (error == ESP_OK) {
        const httpd_uri_t get  = {.uri = "/*", .method = HTTP_GET, .handler = showForm};
        const httpd_uri_t post = {.uri = "/setup", .method = HTTP_POST, .handler = saveForm};
        error                  = httpd_register_uri_handler(sServer, &get);
        if (error == ESP_OK) {
            error = httpd_register_uri_handler(sServer, &post);
        }
    }
    return error;
}

static esp_err_t startDns(void) {
    const struct sockaddr_in address = {
        .sin_family      = AF_INET,
        .sin_port        = htons(kDnsPort),
        .sin_addr.s_addr = inet_addr(DSPET_SETUP_PORTAL_ADDRESS),
    };
    sDnsSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    return sDnsSocket >= 0 && bind(sDnsSocket, (const struct sockaddr *) &address, sizeof(address)) == 0 ? ESP_OK
                                                                                                         : ESP_FAIL;
}

static size_t dnsReply(uint8_t *bytes, size_t length, size_t capacity) {
    // 只处理一个未压缩的标准 DNS 问题，忽略尾随的 EDNS 记录。
    if (length <= kDnsHeaderBytes || (bytes[2] & 0xF8) || bytes[4] != 0 || bytes[5] != 1) {
        return 0;
    }
    size_t offset = kDnsHeaderBytes;
    while (offset < length && bytes[offset]) {
        const size_t labelBytes = bytes[offset++];
        if (labelBytes > 63 || labelBytes > length - offset) {
            return 0;
        }
        offset += labelBytes;
    }
    if (offset >= length || length - offset < 5) {
        return 0;
    }
    ++offset;
    const bool ipv4 = bytes[offset] == 0 && bytes[offset + 1] == 1 && bytes[offset + 2] == 0 && bytes[offset + 3] == 1;
    offset += 4;
    bytes[2] = 0x80 | (bytes[2] & 1);
    bytes[3] = 0;
    memset(bytes + 6, 0, 6);
    const uint8_t answer[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 0, 0, 4, 192, 168, 4, 1};
    if (ipv4 && capacity - offset >= sizeof(answer)) {
        bytes[7] = 1;
        memcpy(bytes + offset, answer, sizeof(answer));
        offset += sizeof(answer);
    }
    return offset;
}

static void pollDns(void) {
    uint8_t bytes[512];
    for (unsigned i = 0; i < 4; ++i) {
        struct sockaddr_in peer;
        socklen_t          peerLength = sizeof(peer);
        const int          count =
            recvfrom(sDnsSocket, bytes, sizeof(bytes), MSG_DONTWAIT, (struct sockaddr *) &peer, &peerLength);
        if (count <= 0) {
            return;
        }
        const size_t reply = dnsReply(bytes, (size_t) count, sizeof(bytes));
        if (reply) {
            sendto(sDnsSocket, bytes, reply, MSG_DONTWAIT, (const struct sockaddr *) &peer, peerLength);
        }
    }
}

esp_err_t DSPetSetupPortalStart(DSPetSetupHotspot *hotspot) {
    uint8_t random[16];
    // 此时无线尚未启动，临时启用硬件熵源生成入网信息。
    bootloader_random_enable();
    esp_fill_random(random, 11);
    bootloader_random_disable();
    memcpy(hotspot->ssid, "DSPet-", 6);
    hexEncode(hotspot->ssid + 6, random + 8, 3);
    hexEncode(hotspot->password, random, 8);
    esp_err_t error = DSPetWifiOpenAccessPoint(hotspot->ssid, hotspot->password);
    if (error == ESP_OK) {
        esp_fill_random(random, sizeof(random));
        hexEncode(sToken, random, sizeof(random));
        atomic_store(&sSaved, false);
        error = startDns();
        if (error == ESP_OK) {
            error = startHttp();
        }
        if (error != ESP_OK) {
            DSPetSetupPortalStop();
        }
    }
    mbedtls_platform_zeroize(random, sizeof(random));
    if (error != ESP_OK) {
        mbedtls_platform_zeroize(hotspot, sizeof(*hotspot));
    }
    return error;
}

bool DSPetSetupPortalPoll(void) {
    pollDns();
    return atomic_load(&sSaved);
}

void DSPetSetupPortalStop(void) {
    if (sServer) {
        httpd_stop(sServer);
        sServer = NULL;
    }
    if (sDnsSocket >= 0) {
        close(sDnsSocket);
        sDnsSocket = -1;
    }
    DSPetWifiClose();
    mbedtls_platform_zeroize(sToken, sizeof(sToken));
}
