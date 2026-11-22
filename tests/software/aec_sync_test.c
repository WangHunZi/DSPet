#include <assert.h>

#include "DSPetAecSync.h"

int main(void) {
    DSPetAecSync sync;
    DSPetAecSyncInit(&sync, 125);
    const int16_t micIn[]    = {100, 101, 102, 103, 104, 105, 106, 107};
    const int16_t refFirst[] = {0, 1, 2, 3, 4, 5, 6, 7};
    const int16_t refNext[]  = {8, 9, 10, 11, 12, 13, 14, 15};
    int16_t       mic[8];
    int16_t       ref[8];
    assert(DSPetAecSyncPush(&sync, kDSPetAecSyncMic, micIn, 8, 40, 1375));
    assert(DSPetAecSyncTake(&sync, mic, ref, 8) == kDSPetAecSyncWaiting);
    assert(DSPetAecSyncPush(&sync, kDSPetAecSyncReference, refFirst, 8, UINT32_MAX, 1000));
    assert(DSPetAecSyncTake(&sync, mic, ref, 8) == kDSPetAecSyncWaiting);
    assert(DSPetAecSyncPush(&sync, kDSPetAecSyncReference, refNext, 8, 0, 1500));
    assert(DSPetAecSyncTake(&sync, mic, ref, 8) == kDSPetAecSyncReady);
    // 两路起点相差 375 µs，扣除 125 µs 后从参考的第 4 个样本读取。
    for (int i = 0; i < 8; ++i) {
        assert(mic[i] == 100 + i);
        assert(ref[i] == 4 + i);
    }
    assert(!DSPetAecSyncPush(&sync, kDSPetAecSyncMic, micIn, 8, 42, 2375));
    return 0;
}
