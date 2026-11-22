#ifndef DSPET_AEC_SYNC_H
#define DSPET_AEC_SYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { kDSPetAecSyncHistorySamples = 1024 };

/** @brief 两个独立的 PCM 来源。 */
typedef enum {
    kDSPetAecSyncMic,       /**< 麦克风。 */
    kDSPetAecSyncReference, /**< 实际送播参考。 */
} DSPetAecSyncSource;

/** @brief 对齐读取结果。 */
typedef enum {
    kDSPetAecSyncWaiting, /**< 两路尚未积攒一帧。 */
    kDSPetAecSyncReady,   /**< 已输出对齐的两路 PCM。 */
    kDSPetAecSyncLost,    /**< 等待的数据已经被覆盖，或帧长度超出容量。 */
} DSPetAecSyncResult;

/** @brief 单路采样缓冲和时间基准，由同步器持有。 */
typedef struct {
    int16_t  samples[kDSPetAecSyncHistorySamples]; /**< 不超过 64 ms 的 PCM 历史。 */
    uint64_t written;                              /**< 已存入的总样本数。 */
    int64_t  startUs;                              /**< 首块起点的估计时刻。 */
    uint32_t nextBlock;                            /**< 下一块的序号，可回绕。 */
} DSPetAecSyncStream;

/** @brief 由调用方持有的双路同步器，同一任务读写。 */
typedef struct {
    DSPetAecSyncStream mic;        /**< 麦克风缓冲。 */
    DSPetAecSyncStream reference;  /**< 播放参考缓冲。 */
    uint64_t           micCursor;  /**< 下一帧麦克风起点。 */
    uint64_t           refCursor;  /**< 下一帧参考起点。 */
    uint32_t           refDelayUs; /**< 麦克风相对选用参考的时间差，单位微秒。 */
    bool               anchored;   /**< 两路时间基准是否已对齐。 */
} DSPetAecSync;

/**
 * @brief 重置同步器，等待首个麦克风块和参考块。
 * @param[out] sync 调用方提供的状态。
 * @param[in] refDelayUs 麦克风相对选用参考的时间差，单位微秒。
 */
void DSPetAecSyncInit(DSPetAecSync *sync, uint32_t refDelayUs);

/**
 * @brief 保存一块共享采样时钟下的单声道 16 kHz PCM。
 * @param[in,out] sync 已初始化的同步器。
 * @param[in] source 来源通道。
 * @param[in] samples PCM 块，调用期间有效。
 * @param[in] sampleCount 样本数，范围为 1 到历史容量。
 * @param[in] blockIndex 通道内连续序号。
 * @param[in] eofUs 完成时刻，单位微秒，来自两路共享的单调时钟。
 * @return 接受返回 true，参数无效或序号不连续时返回 false。
 */
bool DSPetAecSyncPush(DSPetAecSync      *sync,
                      DSPetAecSyncSource source,
                      const int16_t     *samples,
                      size_t             sampleCount,
                      uint32_t           blockIndex,
                      int64_t            eofUs);

/**
 * @brief 读取并消费一帧对齐后的麦克风和参考。
 * @param[in,out] sync 已初始化的同步器。
 * @param[out] mic 麦克风缓冲，容量至少为 sampleCount 个样本。
 * @param[out] reference 参考缓冲，容量至少为 sampleCount 个样本。
 * @param[in] sampleCount 每路读取的样本数。需要覆盖 AEC 一帧。
 * @return 读取状态，只有 Ready 时写入输出。
 */
DSPetAecSyncResult DSPetAecSyncTake(DSPetAecSync *sync, int16_t *mic, int16_t *reference, size_t sampleCount);

#endif
