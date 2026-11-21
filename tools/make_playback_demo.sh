#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .cache
dspetDemoTmp="$(mktemp -d "$PWD/.cache/playback.XXXXXX")"
trap 'rm -rf "$dspetDemoTmp"' EXIT
say -v "${DSPET_DEMO_VOICE:-Tingting}" -r 165     -o "$dspetDemoTmp/demo.aiff"     '你好，我是鲸鱼娘。现在屏幕上的动画还在播放。你可以按一下按键，让我停下来。然后再按一下，我们就能从头再试一遍。'
ffmpeg -nostdin -hide_banner -loglevel error -y -i "$dspetDemoTmp/demo.aiff"     -af volume=-20dB -ac 1 -ar 16000 -f s16le "$dspetDemoTmp/demo.pcm"
test -s "$dspetDemoTmp/demo.pcm"
mv "$dspetDemoTmp/demo.pcm" .cache/playback-demo.pcm
wc -c .cache/playback-demo.pcm
