#include "DSPetAecSync.h"

#include <string.h>

enum { kSampleRateHz = 16000, kUsPerSecond = 1000000, kRingMask = kDSPetAecSyncHistorySamples - 1 };

void DSPetAecSyncInit(DSPetAecSync *sync, uint32_t refDelayUs) {
    memset(sync, 0, sizeof(*sync));
    sync->refDelayUs = refDelayUs;
}

bool DSPetAecSyncPush(DSPetAecSync      *sync,
                      DSPetAecSyncSource source,
                      const int16_t     *samples,
                      size_t             sampleCount,
                      uint32_t           blockIndex,
                      int64_t            eofUs) {
    if (!sync || !samples || !sampleCount || sampleCount > kDSPetAecSyncHistorySamples ||
        (source != kDSPetAecSyncMic && source != kDSPetAecSyncReference)) {
        return false;
    }
    DSPetAecSyncStream *stream = source == kDSPetAecSyncMic ? &sync->mic : &sync->reference;
    if (stream->written) {
        if (blockIndex != stream->nextBlock) {
            return false;
        }
    } else {
        stream->startUs = eofUs - (int64_t) sampleCount * kUsPerSecond / kSampleRateHz;
    }
    for (size_t i = 0; i < sampleCount; ++i) {
        stream->samples[(stream->written + i) & kRingMask] = samples[i];
    }
    stream->written += sampleCount;
    stream->nextBlock = blockIndex + 1;
    return true;
}

DSPetAecSyncResult DSPetAecSyncTake(DSPetAecSync *sync, int16_t *mic, int16_t *reference, size_t sampleCount) {
    if (!sync || !mic || !reference || !sampleCount || sampleCount > kDSPetAecSyncHistorySamples) {
        return kDSPetAecSyncLost;
    }
    if (!sync->anchored) {
        if (!sync->mic.written || !sync->reference.written) {
            return kDSPetAecSyncWaiting;
        }
        // 首块时间戳建立共同时间轴，随后用共享采样时钟和序号推进。
        int64_t scaled  = (sync->mic.startUs - sync->reference.startUs - sync->refDelayUs) * kSampleRateHz;
        int64_t offset  = (scaled + (scaled >= 0 ? kUsPerSecond / 2 : -kUsPerSecond / 2)) / kUsPerSecond;
        sync->micCursor = offset < 0 ? (uint64_t) -offset : 0;
        sync->refCursor = offset > 0 ? (uint64_t) offset : 0;
        sync->anchored  = true;
    }
    if (sync->mic.written < sync->micCursor + sampleCount || sync->reference.written < sync->refCursor + sampleCount) {
        return kDSPetAecSyncWaiting;
    }
    if (sync->mic.written - sync->micCursor > kDSPetAecSyncHistorySamples ||
        sync->reference.written - sync->refCursor > kDSPetAecSyncHistorySamples) {
        return kDSPetAecSyncLost;
    }
    for (size_t i = 0; i < sampleCount; ++i) {
        mic[i]       = sync->mic.samples[(sync->micCursor + i) & kRingMask];
        reference[i] = sync->reference.samples[(sync->refCursor + i) & kRingMask];
    }
    sync->micCursor += sampleCount;
    sync->refCursor += sampleCount;
    return kDSPetAecSyncReady;
}
