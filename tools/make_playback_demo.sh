#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .cache
dspetDemoTmp="$(mktemp -d "$PWD/.cache/playback.XXXXXX")"
trap 'rm -rf "$dspetDemoTmp"' EXIT

render_pcm() {
    local name="$1"
    local line="$2"
    say -v "${DSPET_DEMO_VOICE:-Tingting}" -r 165 -o "$dspetDemoTmp/$name.aiff" "$line"
    ffmpeg -nostdin -hide_banner -loglevel error -y -i "$dspetDemoTmp/$name.aiff" \
        -af volume=-20dB -ac 1 -ar 16000 -f s16le "$dspetDemoTmp/$name.pcm"
    test -s "$dspetDemoTmp/$name.pcm"
    mv "$dspetDemoTmp/$name.pcm" ".cache/$name.pcm"
    wc -c ".cache/$name.pcm"
}

render_pcm playback-demo '你好，我是鲸鱼娘。现在屏幕上的动画还在播放。你可以按一下按键，让我停下来。然后再按一下，我们就能从头再试一遍。'
render_pcm local-voice-reply '我听见啦。现在屏幕上的动画还在播放。你可以直接开口，我会停下来继续听。'
