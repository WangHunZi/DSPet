#include "DSPetAnimation.h"
#include "DSPetIndexedFrame.h"
#include "esp_heap_caps.h"
#include "miniz.h"
#include <stdlib.h>

static uint8_t *sDecoded;
static uint8_t *sPrevious;
static tinfl_decompressor *sInflater;
static size_t sPixelCount;
static const DSPetClip *sClip;
static size_t sLastDecoded = SIZE_MAX;

esp_err_t DSPetAnimationInit(size_t pixelCount)
{
    if (sDecoded) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pixelCount < kDSPetIndexedFramePaletteBytes || pixelCount > SIZE_MAX / 2) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t *decoded = heap_caps_malloc(pixelCount * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint8_t *previous = heap_caps_malloc(pixelCount, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    tinfl_decompressor *inflater = calloc(1, sizeof(*inflater));
    if (!decoded || !previous || !inflater) {
        free(decoded);
        free(previous);
        free(inflater);
        return ESP_ERR_NO_MEM;
    }
    sDecoded = decoded;
    sPrevious = previous;
    sInflater = inflater;
    sPixelCount = pixelCount;
    return ESP_OK;
}

static esp_err_t decodeIndexedFrame(const DSPetClip *clip, size_t index)
{
    uint32_t start = clip->offsets[index];
    uint32_t end = clip->offsets[index + 1];
    if (start >= end || end > (uintptr_t)clip->end - (uintptr_t)clip->start) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t inputBytes = end - start;
    size_t outputBytes = sPixelCount * 2;
    tinfl_init(sInflater);
    tinfl_status status = tinfl_decompress(sInflater, clip->start + start,
        &inputBytes, sDecoded, sDecoded, &outputBytes,
        TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    if (status != TINFL_STATUS_DONE || outputBytes != kDSPetIndexedFramePaletteBytes + sPixelCount ||
        inputBytes != end - start) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    DSPetIndexedFrameRestoreDelta(sDecoded, sPrevious, sPixelCount, index % clip->keyframeInterval == 0);
    return ESP_OK;
}

esp_err_t DSPetAnimationFrameAt(const DSPetClip *clip, int64_t startedUs, int64_t nowUs,
                               DSPetAnimationFrame *frame)
{
    if (!sDecoded) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!clip || !frame || !clip->fps || !clip->frameCount || !clip->keyframeInterval ||
        startedUs < 0 || nowUs < startedUs) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint64_t sequence = (uint64_t)(nowUs - startedUs) * clip->fps / 1000000;
    const size_t index = sequence % clip->frameCount;
    if (sClip != clip || sLastDecoded != index) {
        size_t first = index - index % clip->keyframeInterval;
        if (sClip == clip && sLastDecoded < index && sLastDecoded >= first) {
            first = sLastDecoded + 1;
        }
        // 解码失败后，下一次从关键帧重建，避免使用部分更新的索引。
        sLastDecoded = SIZE_MAX;
        for (size_t i = first; i <= index; ++i) {
            esp_err_t error = decodeIndexedFrame(clip, i);
            if (error != ESP_OK) {
                return error;
            }
        }
        if (!DSPetIndexedFrameExpand(sDecoded, sPixelCount, sPixelCount * 2)) {
            return ESP_ERR_INVALID_SIZE;
        }
        sClip = clip;
        sLastDecoded = index;
    }
    *frame = (DSPetAnimationFrame) {
        .pixels = sDecoded, .sizeBytes = sPixelCount * 2, .index = (unsigned)index,
        .nextFrameUs = startedUs + (int64_t)((sequence + 1) * 1000000 / clip->fps),
    };
    return ESP_OK;
}
