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
板载 Flash 实测 16 MiB，固件使用 8 MiB factory 分区，暂未设置 OTA。

屏幕配置在 `menuconfig → DSPet display`：SPI 速度、mode、Y 偏移、反色与 RGB/BGR。
默认 SPI 10 MHz、mode 0、Y 偏移 0，上电直接播放鲸鱼娘待机动画。
启用 `Show RGB/white/black test` 可在播放前检查色彩。
如果长线导致显示不稳定，可降低 SPI 速度；低于约 6 MHz 时无法维持完整的 6 fps，
播放器会跳过过期帧以保持动作时间。

原版 DSH 插件运行在桌面/浏览器中；这里仅复用它的角色动画素材，
转为 Flash 中逐帧无损压缩的 RGB565，240×240、6 fps、60 帧/约 10 秒循环。
每帧使用 ROM miniz 解压，保留一帧解压缓冲和一帧 DMA 缓冲（各 115,200 字节），
不依赖 PSRAM、网络或板端视频解码器。解压器状态放在堆上，避免挤占任务栈。
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
串口 `pet: Playing frame ...` 只能证明程序持续发送，最终以实物观察为准。

若 USB 自动下载无响应，按住 BOOT、点按 RESET、松开 BOOT 后重试烧录。
这只改变启动模式，不需要修改接线。保留的原固件完整备份位于
`/Users/wanghunzi/CodeX/xiao-s3-st7789-test/backups/original-flash-2026-11-16.bin`。

## 实现顺序

1. ESP-IDF 工程、接线、ST7789 驱动与色彩验证。
2. 复用 dsh-pet 鲸鱼娘动画，添加来源、许可和可复现的素材转换流程。
3. 在屏幕上循环播放角色待机动画。

本阶段不接入网络、模型或音频。麦克风、喇叭留待后续逐步实现。
