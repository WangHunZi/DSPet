#include "DSPetAudioCorrelation.h"

#include <math.h>

bool DSPetAudioCorrelationFind(const int16_t         *reference,
                               size_t                 referenceCount,
                               const int16_t         *mic,
                               size_t                 micCount,
                               size_t                 firstOffset,
                               size_t                 lastOffset,
                               DSPetAudioCorrelation *match) {
    if (!reference || !mic || !match || referenceCount < 2 || referenceCount > micCount || firstOffset > lastOffset ||
        lastOffset > micCount - referenceCount) {
        return false;
    }

    int64_t referenceSum     = 0;
    int64_t referenceSquares = 0;
    int64_t micSum           = 0;
    int64_t micSquares       = 0;
    for (size_t i = 0; i < referenceCount; ++i) {
        int32_t x = reference[i];
        int32_t y = mic[firstOffset + i];
        referenceSum += x;
        referenceSquares += x * x;
        micSum += y;
        micSquares += y * y;
    }
    const double count           = (double) referenceCount;
    const double referenceEnergy = referenceSquares - (double) referenceSum * referenceSum / count;
    if (referenceEnergy <= 0.0) {
        return false;
    }

    bool   found       = false;
    double bestSquared = 0.0;
    size_t bestOffset  = firstOffset;
    for (size_t offset = firstOffset;; ++offset) {
        const double micEnergy = micSquares - (double) micSum * micSum / count;
        if (micEnergy > 0.0) {
            int64_t products = 0;
            for (size_t i = 0; i < referenceCount; ++i) {
                products += (int32_t) reference[i] * mic[offset + i];
            }
            // 去除两路直流。平方后可比较正相和反相，最后只开一次平方根。
            const double covariance = products - (double) referenceSum * micSum / count;
            const double squared    = covariance * covariance / (referenceEnergy * micEnergy);
            if (!found || squared > bestSquared) {
                found       = true;
                bestSquared = squared;
                bestOffset  = offset;
            }
        }
        if (offset == lastOffset) {
            break;
        }
        int32_t outgoing = mic[offset];
        int32_t incoming = mic[offset + referenceCount];
        micSum += incoming - outgoing;
        micSquares += incoming * incoming - outgoing * outgoing;
    }
    if (!found) {
        return false;
    }
    *match = (DSPetAudioCorrelation) {
        .offset = bestOffset,
        .score  = (float) sqrt(bestSquared < 1.0 ? bestSquared : 1.0),
    };
    return true;
}
