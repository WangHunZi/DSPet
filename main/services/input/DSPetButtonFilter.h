#ifndef DSPET_BUTTON_FILTER_H
#define DSPET_BUTTON_FILTER_H

#include <stdbool.h>
#include <stdint.h>

/** @brief 稳定松开后产生的按键事件。 */
typedef enum {
    kDSPetButtonNoEvent,    /**< 尚无完整按键动作。 */
    kDSPetButtonShortPress, /**< 按住不足五秒后松开。 */
    kDSPetButtonLongPress,  /**< 按住至少五秒后松开。 */
} DSPetButtonEvent;

/** @brief 由调用方持有的按键消抖状态。 */
typedef struct {
    bool     rawPressed;    /**< 最近一次原始输入。 */
    bool     stablePressed; /**< 消抖后的电平状态。 */
    bool     armed;         /**< 已释放启动时的按住状态。 */
    uint32_t pressedAtMs;   /**< 本轮按下的时刻，单位毫秒。 */
    uint32_t changedAtMs;   /**< 最近一次电平变化时间，单位毫秒。 */
} DSPetButtonFilter;

/**
 * @brief 使用首个样本初始化消抖器。
 * @param[out] filter 非空状态指针。
 * @param[in] pressed 初始物理按下状态。
 * @param[in] nowMs 初始时刻，单位毫秒。
 */
void DSPetButtonFilterInit(DSPetButtonFilter *filter, bool pressed, uint32_t nowMs);

/**
 * @brief 消费一个采样，在稳定松开后报告短按或长按。
 * @param[in,out] filter 非空的已初始化状态。
 * @param[in] pressed 当前物理按下状态。
 * @param[in] nowMs 当前时刻，单位毫秒，允许计数回绕。
 * @return 按键事件。启动时已按住的那一轮不产生事件。
 */
DSPetButtonEvent DSPetButtonFilterUpdate(DSPetButtonFilter *filter, bool pressed, uint32_t nowMs);

#endif
