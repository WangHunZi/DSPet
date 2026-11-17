# DSPet

XIAO ESP32S3 Plus + 1.3 寸 ST7789（240×240）的桌面角色玩具。
使用 ESP-IDF v6.0.2；分步提交，便于逐个 review。

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
每个动作持续循环；切换时从新动作的第一帧开始，重启后恢复待机。
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
.venv/bin/python -m unittest discover -s tools -p 'test_*.py'
```

需要 ffmpeg（libvpx-vp9）。预生成素材已经提交，直接编译不需要 Python 图像库或 ffmpeg。
CMake 会核对素材尺寸、长度、格式和 SHA-256，不匹配就停止构建。
构建时从 manifest 生成帧数等常量，避免固件与素材各写一套配置。

主机预览：[静态](assets/generated/idle-preview.png) / [动画](assets/generated/idle-preview.gif)。
硬件验收：人物完整、色彩正确、持续循环、有呼吸/眨眼等动作，无花屏或重启。
短按 BOOT 六次，确认六个动作依次切换并回到待机；按住不重复切换。
串口会输出 `Action: <动作标识>`，用于核对实际按键切换。
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

本阶段不接入网络、模型或音频。麦克风、喇叭留待后续逐步实现。
