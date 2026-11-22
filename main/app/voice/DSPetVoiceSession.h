#ifndef DSPET_VOICE_SESSION_H
#define DSPET_VOICE_SESSION_H

#include <stdbool.h>
#include <stdint.h>

/** @brief 应用层语音会话状态。 */
typedef enum {
    kDSPetVoiceIdle,      /**< 等待唤醒或手动发起。 */
    kDSPetVoiceListening, /**< 等待开口或录制本句。 */
    kDSPetVoiceWaiting,   /**< 录音完成，等待本轮回复。 */
    kDSPetVoiceSpeaking,  /**< 播报本轮回复，仍监听插话。 */
} DSPetVoiceState;

/** @brief 会话接收的输入事件。 */
typedef enum {
    kDSPetVoiceStart,         /**< 发起新会话。 */
    kDSPetVoiceSpeechStart,   /**< 检测到用户开口。 */
    kDSPetVoiceSpeechEnd,     /**< 本句录音完成。 */
    kDSPetVoiceReplyReady,    /**< 一轮回复可播放。 */
    kDSPetVoiceReplyEnd,      /**< 一轮回复已播完。 */
    kDSPetVoiceReplyIgnored,  /**< 等待的回合无须播报。 */
    kDSPetVoiceReplyFailed,   /**< 一轮请求或播报失败，回到聆听。 */
    kDSPetVoiceListenTimeout, /**< 聆听超时且未开口。 */
    kDSPetVoiceStop,          /**< 停止会话或处理出错。 */
} DSPetVoiceEvent;

/** @brief 由会话控制任务维护的纯状态，初始可置零。 */
typedef struct {
    DSPetVoiceState state;           /**< 当前状态。 */
    uint32_t        turnId;          /**< 回合编号，结束会话后保留用于生成下一个编号。 */
    bool            alwaysListening; /**< 开启时不因安静退回唤醒待机。 */
    bool            recording;       /**< 聆听阶段已确认开口。 */
} DSPetVoiceSession;

/**
 * @brief 应用一个会话事件。
 * @param[in,out] session 负责产生回合编号的会话状态。
 * @param[in] event 当前输入事件。
 * @param[in] replyTurnId 回复和播报事件携带的编号，其余事件填零。
 * @return 接受事件为 true。状态不允许或回复已过期时为 false。
 */
bool DSPetVoiceSessionApply(DSPetVoiceSession *session, DSPetVoiceEvent event, uint32_t replyTurnId);

#endif
