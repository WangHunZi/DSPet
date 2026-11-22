#ifndef DSPET_ACTION_STATE_H
#define DSPET_ACTION_STATE_H

#include <stdbool.h>
#include <stdint.h>

/** @brief 应用持有的动作选择状态。 */
typedef struct {
    unsigned index;       /**< 当前动作序号。 */
    uint32_t lastPresses; /**< 上次处理的累计按键次数。 */
    int64_t  startedUs;   /**< 当前动作开始时刻，单位微秒。 */
} DSPetActionState;

/**
 * @brief 仲裁手动切换和自动到期，两者同时发生时以按键为准。
 * @param[in,out] state 动作选择状态。
 * @param[in] clipCount 可选动作数量，必须大于零。
 * @param[in] presses 当前累计按次，允许回绕。
 * @param[in] nowUs 当前时间，单位微秒。
 * @param[in] durationUs 当前动作时长，单位微秒。
 * @param[in] automatic 是否自动切换。
 * @return 开始了新一轮播放时返回 true。
 */
bool DSPetActionStateUpdate(
    DSPetActionState *state, unsigned clipCount, uint32_t presses, int64_t nowUs, int64_t durationUs, bool automatic);

#endif
