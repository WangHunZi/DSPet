# DSPet

XIAO ESP32S3 Plus + 1.3 寸 ST7789（240×240）的桌面角色玩具。
使用 ESP-IDF v6.0.2；分步提交，便于逐个 review。
新增固件代码和文档示例遵循 [DSPet 代码风格](docs/coding-style.md)。

## 接线

| 屏幕 | XIAO | GPIO |
| --- | --- | --- |
| GND | GND | — |
| 3V3 | 3V3 | — |
| BL | D5 | 6 |
| RESET | D4 | 5 |
| RS/DC | D3 | 4 |
| MOSI | D10 | 9 |
| SCK | D8 | 7 |
| CS | D9 | 8 |

MISO 禁用，D9 用作 CS；RESET 接 D4。

## 构建

```sh
bash scripts/idf.sh build
bash scripts/idf.sh flash-usb /dev/cu.usbmodem1101
bash scripts/idf.sh -p /dev/cu.usbmodem1101 monitor
```

脚本默认 SDK 在 `~/esp-idf`，可用 `IDF_PATH`、`IDF_PYTHON_ENV_PATH` 覆盖。
串口监视器按 Ctrl+] 退出。USB 串口名可能改变。
板载 Flash 实测 16 MiB，固件使用 15 MiB factory 分区，暂未设置 OTA。
扩大的分区用于容纳更多动作，NVS 和校准分区的位置不变。

屏幕配置在 `menuconfig → DSPet display`：SPI 速度、mode、Y 偏移、反色与 RGB/BGR。
默认 SPI 40 MHz、mode 0、Y 偏移 0，CPU 240 MHz，上电直接播放鲸鱼娘待机动画。
**运行时短按 BOOT：待机 → 挥手 → 伸懒腰 → 跳舞 → 吃饭 → 睡觉 → 待机。** 不需要额外接线。
默认自动轮换：每段完整播放约 10 秒，再进入下一段；BOOT 可随时提前切换。
切换后重新计算下一段的播放时间，重启后恢复待机。
可在 `menuconfig → DSPet playback` 关闭自动轮换，改为手动选择后持续循环。
BOOT 是 GPIO0，RESET 是重启键；不要同时按这两个键来切动作。
按键每 10 ms 采样，稳定 30 ms 才触发；长按只切一次，松开后可再按。
独立按键任务在 SPI 传输期间仍可采样，切换在当前帧发送完成后生效。
启用 `Show RGB/white/black test` 可在播放前检查色彩。
如果长线导致显示不稳定，可降低 SPI 速度；低于约 24 MHz 时无法维持完整的 24 fps，
播放器会跳过过期帧以保持动作时间。

原版 DSH 插件运行在桌面/浏览器中；这里仅复用它的角色动画素材，
转为 Flash 中差分压缩的调色板动画，每个动作 240×240、24 fps、约 240 帧/10 秒。
每段共用 128 色调色板（有色彩量化），相邻帧索引 XOR 差分后 zlib 压缩。
每 24 帧为一个关键帧；切换、跳帧、循环都从正确的关键帧重建，防止花屏。
每帧使用 ROM miniz 解压并展开为 RGB565，保留一帧解压缓冲和一帧 DMA 缓冲（各 115,200 字节），
另保留 57,600 字节前帧索引；不依赖 PSRAM、网络或板端 WebM 解码器。
解压器状态放在堆上，避免挤占任务栈。
单帧全屏为 115,200 字节；40 MHz SPI 的纯传输时间约 23 ms。
24 fps 的帧预算约 41.7 ms，实际还需包含解压和颜色展开时间，最终以实测为准。
来源及非商业素材许可见 [assets/README.md](assets/README.md)。

## 素材与验证

```sh
python3 -m venv .venv
.venv/bin/pip install -r tools/requirements.txt
.venv/bin/python tools/convert_animation.py
.venv/bin/python -m unittest discover -s tests/software -p 'test_*.py'
```

需要 ffmpeg（libvpx-vp9）。预生成素材已经提交，直接编译不需要 Python 图像库或 ffmpeg。
CMake 会核对素材尺寸、长度、格式和 SHA-256，不匹配就停止构建。
构建时从 manifest 生成帧数等常量，避免固件与素材各写一套配置。

主机预览：[静态](assets/generated/idle-preview.png) / [动画](assets/generated/idle-preview.gif)。
硬件验收：人物完整、色彩正确、持续循环、有呼吸/眨眼等动作，无花屏或重启。
本机已验证自动轮换与 BOOT 切换，日志样本约 23.7～23.9 fps；
详见 [2026-11-17 实物验证](docs/validation-2026-11-17.md)。
短按 BOOT 六次，确认六个动作依次切换并回到待机；按住不重复切换。
串口会输出 `Action: <动作标识>`，用于核对实际按键切换。
不按键等待约一分钟，确认六个动作自动轮换；按键与自动到期重合时只前进一段。
串口每 3 秒记录实测 fps、平均解码与 SPI 耗时；这些只能证明程序发送速度，
花屏、撕裂与画面完整性仍需看实物。

若 USB 自动下载无响应，按住 BOOT、点按 RESET、松开 BOOT 后重试烧录。
这只改变启动模式，不需要修改接线。保留的原固件完整备份位于
`/Users/wanghunzi/CodeX/xiao-s3-st7789-test/backups/original-flash-2026-11-16.bin`。

## 实现顺序

1. ESP-IDF 工程、接线、ST7789 驱动与色彩验证。
2. 复用 dsh-pet 鲸鱼娘动画，添加来源、许可和可复现的素材转换流程。
3. 在屏幕上循环播放角色待机动画。
4. 无损压缩帧、添加挥手和睡觉素材、BOOT 按键消抖切换（分别提交）。

## 离线音频自检

| 信号 | XIAO ESP32S3 Plus | 模块 |
| --- | --- | --- |
| BCLK | D0 / GPIO1 | MAX98357A BCLK、INMP441 SCK |
| WS | D1 / GPIO2 | MAX98357A LRC、INMP441 WS |
| 播放数据 | D6 / GPIO43 | MAX98357A DIN |
| 录音数据 | D7 / GPIO44 | INMP441 SD |
| 电源 | 3V3 / GND | 各模块供电和共地，INMP441 L/R 接 GND |

喇叭的两根线分别接 MAX98357A 的 SPK+ 和 SPK−。
开机或按一下 RESET 后，先播放一声低音量提示音，约 0.4 秒后录音 3 秒，
接着回放，再进入动画。听到提示音后说一句话即可核对录音和回放。
I²S 采用 16 kHz、Philips 格式、双声道 32 位槽，录音取左槽。

串口 `Speaker test frames sent` 只确认驱动已发送。
`Mic captured` 会打印采样数、直流偏置、峰值和 RMS，是否能正常听到以实物为准。
录音只暂存在 RAM，回放完成后释放。可在 `menuconfig → DSPet audio` 关闭开机自检。

## 本地播报与停播演示

在 Mac 上运行 `bash tools/make_playback_demo.sh`，用系统中文语音生成约 13 秒的测试素材。
素材是 16 kHz、单声道、PCM16LE，存放在 `.cache/playback-demo.pcm`。
然后在 `bash scripts/idf.sh menuconfig` 的 `DSPet audio` 菜单打开 `Use BOOT to play or stop a local speech demo`，重新构建并烧录。

1. 等待桌宠动画出现。空闲时短按 BOOT，从头播放测试句，屏幕显示挥手动作。
2. 她说到一半时短按 BOOT，确认旧音频停止，屏幕回到当前待机动作。
3. 再按一次，确认能从头播放。让这一次自然播完，确认也能恢复待机动作。

演示使用独立的 PCM 播放任务。`First PCM block queued` 表示首块已入队。
`Finished; DMA drained` 和 `Cancelled; DMA drained` 分别对应自然结束和取消完成。
停播延迟日志从应用发出取消请求算到 DMA 排空确认，实际停声仍需要听喇叭验证。
演示开关默认关闭。关闭时 BOOT 用于切换动画。

## 双路音频采样诊断

在同一 `DSPet audio` 菜单打开 `Measure microphone and playback DMA timing in the speech demo`。
XIAO ESP32S3 Plus 的录音内存测试使用 Octal PSRAM，选择 `Add RAM to heap_caps allocator`。
麦克风左槽和实际经过 TX DMA 的 PCM 都以 16 kHz、每块 128 样本记录在板端有界队列。
每两秒输出块数、丢块、块间隔、DMA 完成时间的相位差、PCM 峰值和最低内部堆。
串口只输出统计信息。

等动画开始后，先在待机时对麦克风说话，确认 `peak_mic/ref` 的参考端仍为 0。
短按 BOOT 开始播报，在她说话时再对麦克风说一句话，确认两路同时有数据。
再按 BOOT 停播，等 `TX reference silent`。重播一遍并等出现 `Finished; DMA drained`。
`phase_us` 是两路 DMA 完成节拍的相位估计，做回声消除时还需校准实际路径延迟。
也可以打开 `Automatically exercise playback, cancellation and normal completion once`。
每次开机自检后会执行一轮自动验证，按 BOOT 可转为手动操作。

## 板载串口测试

默认固件在串口提供 `dspet help` 和 `dspet info`。实机测试版使用独立的构建目录和配置，并默认启用本地会话。

```sh
bash tools/make_playback_demo.sh
bash scripts/idf.sh -B .cache/build-serial-voice \
  -D SDKCONFIG=.cache/sdkconfig.serial-voice \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;tests/sdkconfig.serial.defaults' \
  -p /dev/cu.usbmodem1101 flash monitor
```

串口出现 `xiao>` 后输入以下命令。将 `2` 替换成录音回放用例实际给出的运行编号。
独立音频用例需要先执行 `dspet voice disable`，等 `dspet voice status` 显示 `mode=off`。复测结束用 `dspet voice enable` 恢复唤醒。

```text
dspet help
dspet info
dspet test hardware audio duplex
dspet test hardware audio alignment
dspet test hardware audio aec
dspet test hardware audio barge-in-quiet
dspet test human audio barge-in
dspet test human audio barge-in-keyboard
dspet test status
dspet test human audio loopback
dspet test confirm 2 pass
```

双路用例播放固定语音、取消并重播到自然结束。固件检查 PSRAM、RX/TX 连续性和 TX 静音。
延迟用例播放三段 128 ms 短扫频。把设备保持在日常摆放位置，测试期间保持安静。
每轮输出 `delay_us` 和 `correlation_milli`，分别表示 TX 参考到麦克风的估计延迟和相关系数乘以 1,000。
`alignment_spread_us` 表示三轮最大值和最小值的差。估计包含换能器、声学路径和 DMA 时间基准的偏差。
AEC 用例播放固定语音，请在测试期间保持安静。它先测底噪，跳过前两秒自适应，再比较同一段麦克风 PCM 在处理前后的 RMS。
底噪输出整段 RMS 和帧 RMS 中位数，用后者判断回录里是否有足够可测的回声。
`reduction_db` 记录抑制量，`max_process_us` 与 `frame_budget_us` 用于检查实时性。
此用例要求回录有足够回声、抑制至少 6 dB、无丢块，且处理时间小于一帧时长。
测试配置启用 ESP-SR 2.6.1 的双工低开销 AEC。若复用已有的测试 `sdkconfig`，在 `DSPet audio` 菜单启用 AEC 服务后再构建。
自然插话的安静用例要求她自己说完且没有误停。人工插话用例在她播报中途直接说「等一下，我问的是明天」。
她应该停播并继续听，句尾停顿后回放。用 `dspet test confirm <实际编号> pass|fail` 确认是否听清整句话。
当前本地原型从首块播报入队约 850 ms 后开始接受插话，保留约 320 ms 的句首预录。确认开口后，续录采用较低的能量门限，连续 700 ms 安静时收尾。
键盘用例要求在播报中途正常打字几秒并保持不说话，播完后再确认。串口报告 `cancel_to_drain_us`、预录样本数和连续性错误，停播耗时从应用发出取消请求开始计算。
回放用例提示你在提示音后说话，回放后进入 `waiting_human`，能听清输入 `pass`，否则输入 `fail`。
同一时间接受一个用例。`DSPET_TEST` 行带运行编号和状态，`DSPET_TEST_METRIC` 行报告双路测量值。
运行编号和状态保存在 RAM，重启后重新计数。如需留存整轮证据，可用电脑保存串口输出。

主机测试在 `tests/software/`，板上自动用例在 `tests/hardware/`，人工回放用例在 `tests/human/`。
主机脚本可以在串口通过 CR 发送和终端相同的命令，记录 `DSPET_TEST` 结果。

### 本地语音会话

短按 BOOT 或用 `dspet voice start` 开始本地会话。动作切到聆听后正常说一句。桌宠会播固定中文答复，答完自动继续听。播报中直接开口可插话，前一次播报停止后会录完新一句。连续 8 秒未开口时返回待机。

会话固件使用 `CONFIG_DSPET_LOCAL_VOICE`。串口 `DSPET_VOICE` 记录状态和回合编号，`DSPET_VOICE_CAPTURE` 报告采样数和是否达到 4 秒上限，`DSPET_VOICE_DRAIN` 报告取消请求到 DMA 排空的间隔。

关闭其他串口终端后，用安装了 pyserial 的 Python 执行下列两个用例。第一条先安静等待超时，再自动跑双路播放诊断并恢复语音服务。第二条按提示短按一次 BOOT、连续说三句，在第二次播报中途插话，听完第三次回复后保持安静。脚本核对串口状态，实际听感由参与测试的人确认。

```sh
python3 tests/hardware/verify_local_voice.py --port /dev/cu.usbmodem1101
python3 tests/human/verify_local_voice_session.py --port /dev/cu.usbmodem1101
```

## Mac 语音网关

使用 Python 3.10 或更新版本和 FFmpeg。从仓库根目录运行。角色提示在 [gateway/system_prompt.txt](gateway/system_prompt.txt)，由同一会话中的每轮对话复用。下面把两段 WAV 依次送入同一会话，终端显示转写、回复和耗时，每轮输出一个可播放的 WAV。

```sh
python3 -m gateway --ask-key wav 问题.wav 追问.wav --output-dir .cache/voice-replies
```

输入 WAV 为 16 kHz、单声道、16 位 PCM。网关使用 DeepSeek 的识别、对话和语音合成，合成音色为 Stella。可通过 `DEEPSEEK_API_KEY` 提供密钥，或用 `--ask-key` 隐藏输入。启动 HTTP 接口时另设设备凭据。

```sh
export DSPET_DEVICE_TOKEN="$(openssl rand -hex 24)"
python3 -m gateway --ask-key serve
```

HTTP 默认监听 `127.0.0.1:8765`。板端联网联调时可通过 `--bind` 指定 Mac 的局域网地址。请求使用 `Authorization: Bearer <设备凭据>`。

| 请求 | 输入和结果 |
| --- | --- |
| `GET /health` | 返回网关进程状态。 |
| `POST /v1/turns` | `X-Session-Id` 在一次唤醒内保持相同，`X-Turn-Id` 为递增的正整数。请求体用 `Content-Length` 提交最多 15 秒的 16 kHz 单声道 PCM16LE，类型为 `application/octet-stream`。成功响应分块返回同格式 PCM，并回传 `X-Turn-Id`、`X-PCM-Rate`。 |
| `DELETE /v1/turns/<回合编号>` | 带上 `X-Session-Id`。取消正在生成或传输的回合，或把已生成的回复标记为中断。设备应先本地停播，并在提交下一轮前发出取消。 |

音频开始前的失败以 JSON 返回 `error.code` 和 `error.message`。音频开始后的失败会关闭连接，完整响应有 HTTP 分块结束标记。已取消的回合及迟到数据通过回合编号识别。会话历史存在网关内存中，同一会话保留最近六轮；网关日志只记录回合编号、耗时、字节数和错误码。

运行 `python3 -m unittest discover -s tests/software -p test_gateway.py` 可用模拟上游复测。测试覆盖会话历史、异常返回、收到首块后取消，以及取消旧回合后继续对话。

## XIAO 直连 DeepSeek 验证

直连测试版加入 Wi-Fi 和 HTTPS。它保留板端本地会话，并提供一个独立用例，通过固定文本问题验证对话、TTS 和喇叭播放。首次安装此配置时执行完整烧录，写入新的分区表和唤醒模型。

```sh
bash tools/make_playback_demo.sh
bash scripts/idf.sh -B .cache/build-direct -D SDKCONFIG=.cache/sdkconfig.direct \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;tests/sdkconfig.serial.defaults;tests/sdkconfig.direct.defaults' \
  -p /dev/cu.usbmodem1101 flash
```

关闭占用串口的终端，使用安装了 pyserial 的 Python。先按家中 SSID 前缀扫描 2.4 GHz 网络，再使用扫描到的完整 SSID。

```sh
python3 tests/hardware/verify_direct_voice.py --port /dev/cu.usbmodem1101 scan 家中网络前缀
python3 tests/hardware/verify_direct_voice.py --port /dev/cu.usbmodem1101 direct 实际SSID
```

直连用例通过隐藏输入取得 Wi-Fi 口令和 DeepSeek Key，串口日志只显示阶段结果。Wi-Fi 使用 RAM 配置，释放测试资源时擦除凭据。串口同时提供当前时间供板端校验证书有效期。测试先暂时关闭本地语音，收完整段合成音频后重采样播放，结束后恢复原来的唤醒模式。`direct` 可加 `--short` 验证一字回复。串口报告 Wi-Fi 信道、TLS 握手、内部堆、HTTP 状态及首批 PCM 排入 I²S 的耗时。

### 直连语音会话

同一测试固件还可以启用直连会话。下面先用板载已有音频经过真实 ASR、对话、流式合成和喇叭播放。加 `--keep-enabled` 后固件继续保持直连，供后续真人用例使用。

```sh
python3 tests/hardware/verify_direct_conversation.py --port /dev/cu.usbmodem1101 \
  --ssid 实际SSID --keep-enabled --exercise-cancel
python3 tests/human/verify_direct_conversation_human.py --port /dev/cu.usbmodem1101
```

`--exercise-cancel` 在自动播报中再次提交板载 PCM，验证旧流关闭、DMA 排空和新回合的识别。真人插话用例在手动直连模式下先短按 BOOT，接着请桌宠讲个故事。听到开始播报后等两三秒，再直接说「等一下，一加一等于几」。听桌宠回答后保持安静。串口记录 ASR 文本、首块播放、插话前后的回合编号和旧音频排空。

屏幕在等待云端时显示一张清醒的待机画面。回复音频就绪后先刷出挥手素材的笑脸关键帧，再播声音，播完回到聆听。首帧确认最多等待 2 秒。自动直连用例检查画面切换和声音的先后。

录音和回声参考保持 16 kHz。ASR 上传副本转成 8 kHz，TTS 音频分块转换成 16 kHz。网络任务和播放任务通过 PSRAM 中的环形缓冲交接。接收跟得上时积累约两秒音频再开播，接收不足时等待整段完成或缓冲接近上限。发生断粮时也等待这个条件，再续播剩余音频。关闭直连用 `dspet voice disable`，再用 `dspet voice enable` 回到本地唤醒模式。重启后临时凭据需重新配置。

### 免唤醒直连

直连进入待机后，在板载 Shell 用 `dspet voice always on` 开启。等开口时屏幕保留清醒待机画面，可以直接对桌宠说话。检测到开口后显示收音动作，串口状态变为 `recording`。`dspet voice status` 显示 `always=1`。`dspet voice always off` 停止当前回合并回到手动启动的待机。串口手动开启的免唤醒随会话结束。配网固件每次联网后自动开启免唤醒。

板端检测人声并分句。候选录音送到 DeepSeek ASR，DeepSeek V4 Flash 根据转写和最近问答决定是否回应。没点名的提问、招呼和日常感叹也可以接话，听不出意思的片段和明确的私人备忘可以静默。忽略时串口记录 `direct_ignored`，随后继续聆听。安静 8 秒以后仍然可以直接说话，下一轮用新的对话上下文。等待一轮网络回复时暂不接收下一句，开始播报后仍可本地插话。

自动实机检查需要保持安静。以下命令先测普通直连，再验证跨过原有聆听超时后仍能提交回合、处理板载短句后继续聆听、以及关闭免唤醒后回到手动启动的待机。

```sh
python3 tests/hardware/verify_direct_conversation.py --port /dev/cu.usbmodem1101 \
  --ssid 实际SSID --exercise-always --keep-enabled
```

可用 `python3 tests/human/verify_direct_conversation_human.py --port /dev/cu.usbmodem1101 --recording-display` 验收等开口、收音和回复结束后的画面。

真人检查时再次开启免唤醒，直接问「你是谁？你最爱吃什么？」，听她是否按角色回答。之后可以随口说「今天有点闷」，检查没点名时也能接话。遇到静默时，通过串口的 ASR 和 `direct_ignored` 日志区分识别与回应结果。

### 手机配网与恢复初始状态

给直连构建追加 `tests/sdkconfig.provisioning.defaults`，并使用新的配置文件。

```sh
bash scripts/idf.sh -B .cache/build-setup -D SDKCONFIG=.cache/sdkconfig.setup \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;tests/sdkconfig.serial.defaults;tests/sdkconfig.direct.defaults;tests/sdkconfig.provisioning.defaults' \
  build
```

1. 首次开机屏幕显示 Wi-Fi 二维码。用手机相机扫描并加入临时 `DSPet-…` 热点。
2. 在弹出的配网页填写 2.4 GHz Wi-Fi 名称、密码和 DeepSeek API Key。如果手机没有自动弹窗，保持连接该热点，在浏览器打开屏幕上的 `http://192.168.4.1/`。
3. 点「保存并联网」。设备把三项配置保存到 Flash 的 NVS 并重启。设备连上 Wi-Fi 并完成 SNTP 校时后进入免唤醒聆听，直接对鲸鱼娘说话即可。连接或校时失败时会重新显示配网二维码，便于更正配置。
4. 要恢复初始状态，在设备运行时按住 BOOT。满 5 秒立即清除配置，松手后重启并重新显示配网二维码。按住不足 1 秒后松开算短按。

板载 Shell 的 `dspet setup status` 显示配网、连接中或已上线。USB 串口的 `dspet setup hotspot` 可以按需查看一次性的热点口令。已保存的 Wi-Fi 密码和 API Key 不通过串口或网页回显。此配置使用 NVS 擦除和清理已删除的数据，尚未启用 Flash 静态加密。

板载配网测试命令

- `dspet test hardware setup storage` 在独立测试命名空间验证保存、读回和清除，板端配置可继续使用。
- `dspet test hardware setup button` 读取 BOOT 当前电平、短按次数和已达标的长按次数。物理长按仍会触发恢复出厂，确认长按时先用测试配置。
