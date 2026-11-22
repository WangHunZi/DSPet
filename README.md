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

默认固件在串口提供 `dspet help` 和 `dspet info`。实机测试版使用独立的构建目录和配置，不需要改日常固件的 `sdkconfig`。

```sh
bash tools/make_playback_demo.sh
bash scripts/idf.sh -B .cache/build-serial-tests \
  -D SDKCONFIG=.cache/sdkconfig.serial-check \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;tests/sdkconfig.serial.defaults' \
  -p /dev/cu.usbmodem1101 flash monitor
```

串口出现 `xiao>` 后输入以下命令。将 `2` 替换成录音回放用例实际给出的运行编号。

```text
dspet help
dspet info
dspet test hardware audio duplex
dspet test status
dspet test human audio loopback
dspet test confirm 2 pass
```

双路用例播放固定语音、取消并重播到自然结束。固件检查 PSRAM、RX/TX 连续性和 TX 静音。
回放用例提示你在提示音后说话，回放后进入 `waiting_human`，能听清输入 `pass`，否则输入 `fail`。
同一时间接受一个用例。`DSPET_TEST` 行带运行编号和状态，`DSPET_TEST_METRIC` 行报告双路测量值。
运行编号和状态保存在 RAM，重启后重新计数。如需留存整轮证据，可用电脑保存串口输出。

主机测试在 `tests/software/`，板上自动用例在 `tests/hardware/`，人工回放用例在 `tests/human/`。
主机脚本可以在串口通过 CR 发送和终端相同的命令，记录 `DSPET_TEST` 结果。
