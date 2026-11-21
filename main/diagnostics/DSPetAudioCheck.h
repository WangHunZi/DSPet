#ifndef DSPET_AUDIO_CHECK_H
#define DSPET_AUDIO_CHECK_H

/**
 * @brief 播提示音，录音三秒并回放，然后释放音频设备。
 * @note 开机时在应用任务调用。程序记录错误并返回，随后可启动动画。
 */
void DSPetAudioCheckRun(void);

#endif
