#include "DSPetPcmBufferPolicy.h"

enum {
    kMinimumObservationMs = 1200,
    kMinimumPlaybackBytes = 64000,
    kRequiredBytesPerMs   = 36,
    kHeadroomBytes        = 32000,
};

bool DSPetPcmBufferCanResume(size_t bytes, size_t capacityBytes, bool complete) {
    return complete || (bytes && capacityBytes - bytes <= kHeadroomBytes);
}

bool DSPetPcmBufferCanStart(size_t bytes, size_t capacityBytes, uint32_t elapsedMs, bool complete) {
    if (!bytes) {
        return false;
    }
    if (DSPetPcmBufferCanResume(bytes, capacityBytes, complete)) {
        return true;
    }
    return bytes >= kMinimumPlaybackBytes && elapsedMs >= kMinimumObservationMs &&
           (uint64_t) bytes >= (uint64_t) elapsedMs * kRequiredBytesPerMs;
}
