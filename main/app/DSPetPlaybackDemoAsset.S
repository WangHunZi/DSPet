    .section .rodata.dspet_playback_demo,"a",@progbits
    .balign 4
    .global dspetPlaybackDemoStart
    .global dspetPlaybackDemoEnd
dspetPlaybackDemoStart:
    .incbin DSPET_PLAYBACK_DEMO_PCM_PATH
dspetPlaybackDemoEnd:
