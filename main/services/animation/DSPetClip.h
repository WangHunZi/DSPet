#ifndef DSPET_CLIP_H
#define DSPET_CLIP_H

#include <stdint.h>

/** @brief 由构建系统验证并常驻 Flash 的动画片段。 */
typedef struct {
    const char     *name;             /**< 动作名称。 */
    const uint8_t  *start;            /**< 压缩数据起始地址。 */
    const uint8_t  *end;              /**< 压缩数据结束地址，指向末字节之后。 */
    const uint32_t *offsets;          /**< 每帧起始偏移，最后一项为总字节数。 */
    unsigned        frameCount;       /**< 帧数。 */
    unsigned        fps;              /**< 每秒帧数。 */
    unsigned        keyframeInterval; /**< 两个关键帧之间的帧数。 */
} DSPetClip;

#endif
