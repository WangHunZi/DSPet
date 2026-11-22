# DSPet · 鲸鱼娘桌宠

用 XIAO ESP32S3 Plus 复刻一只会聊天的桌宠。手机扫码配网后，直接对鲸鱼娘说话。设备通过 DeepSeek 完成语音识别、对话和播报。

## 下载固件并刷机

- [Release 固件](https://github.com/WangHunZi/DSPet/releases/download/v0.1.0/dspet-v0.1.0-xiao-esp32s3-plus-release.zip) · 日常使用，第一次复刻建议下载这个版本。
- [Debug 固件](https://github.com/WangHunZi/DSPet/releases/download/v0.1.0/dspet-v0.1.0-xiao-esp32s3-plus-debug.zip) · 串口联调，日志更详细。

适用于 16 MB Flash 的 XIAO ESP32S3 Plus。解压 ZIP，用 USB 数据线连接板子。需要 Python 和 esptool，可在 Python 虚拟环境中用 `python -m pip install esptool` 安装。以下以 Release 为例，把串口换成实际端口（如 `/dev/cu.usbmodem*` 或 `COM3`）。

```sh
python -m esptool --chip esp32s3 --port /dev/ttyACM0 --before usb-reset --after hard-reset \
  write-flash 0x0 dspet-v0.1.0-xiao-esp32s3-plus-release.bin
```

刷 Debug 时换成包内的 Debug 文件名。完整镜像会清除板上已有的 Wi-Fi 和 API Key。若无法进入下载模式，按住 BOOT，点按 RESET，松开 BOOT 后重试。烧录完成后按[配网步骤](#配网与使用)扫码。

![鲸鱼娘待机动画](assets/generated/idle-preview.gif)

## 材料与接线

- Seeed Studio XIAO ESP32S3 Plus（16 MB Flash、8 MB PSRAM），配 Wi-Fi 天线和 USB 数据线
- 1.3 寸 ST7789 SPI 屏幕（240×240、3.3 V）
- INMP441 麦克风、MAX98357A 功放和小喇叭

| ST7789 | XIAO |
| --- | --- |
| GND、3V3 | GND、3V3 |
| BL | D5（GPIO6） |
| RESET | D4（GPIO5） |
| RS / DC | D3（GPIO4） |
| MOSI | D10（GPIO9） |
| SCK | D8（GPIO7） |
| CS | D9（GPIO8） |

| 音频信号 | XIAO | 模块引脚 |
| --- | --- | --- |
| 时钟 | D0（GPIO1） | MAX98357A BCLK、INMP441 SCK |
| 字选 | D1（GPIO2） | MAX98357A LRC、INMP441 WS |
| 播放数据 | D6（GPIO43） | MAX98357A DIN |
| 录音数据 | D7（GPIO44） | INMP441 SD |
| 电源 | 3V3、GND | 两个模块都接 3.3 V 并共地 |

INMP441 的 L/R 接 GND。喇叭接 MAX98357A 的 SPK+ 和 SPK−。

## 编译与烧录

准备 [ESP-IDF v6.0.2](https://github.com/espressif/esp-idf/releases/tag/v6.0.2) 和 FFmpeg。编译命令在已激活 ESP-IDF 环境的终端运行。

```sh
git clone https://github.com/WangHunZi/DSPet.git
cd DSPet
```

预生成的动画已随仓库提供。编译前需要生成两段板载测试音频。在 macOS 上可用 `bash tools/make_playback_demo.sh`。也可以把自己录制的至少 3 秒语音放在 `demo.wav`，用 FFmpeg 生成。

```sh
mkdir -p .cache
ffmpeg -i demo.wav -af volume=-20dB -ac 1 -ar 16000 -c:a pcm_s16le -f s16le .cache/local-voice-reply.pcm
cp .cache/local-voice-reply.pcm .cache/playback-demo.pcm
```

将 `PORT` 改为板子的 USB 串口。Linux 常见 `/dev/ttyACM0`，macOS 常见 `/dev/cu.usbmodem*`。

```sh
PORT=/dev/ttyACM0
idf.py -B .cache/build-setup -D SDKCONFIG=.cache/sdkconfig.setup \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;tests/sdkconfig.serial.defaults;tests/sdkconfig.direct.defaults;tests/sdkconfig.provisioning.defaults' \
  -p "$PORT" flash monitor
```

串口监视器用 Ctrl+] 退出。

## 配网与使用

1. 手机扫屏幕二维码并加入临时 `DSPet-…` 热点。如果网页没有自动弹出，保持连接该热点并打开 `http://192.168.4.1/`。
2. 填写 **2.4 GHz Wi-Fi** 的名称、密码和 DeepSeek API Key，点击「保存并联网」。
3. 等桌宠重启联网，直接对鲸鱼娘说话。串口的 `dspet setup status` 显示 `online` 时可以开始对话。
4. 更换 Wi-Fi 或 Key 时，按住 BOOT 满 5 秒清除配置，松手后重启并重新扫码。

凭据保存在设备的明文 NVS。串口可用 `dspet help` 查看命令。

## 许可

DSPet 原创代码与文档使用 [MIT License](LICENSE)。鲸鱼娘动画及其转换素材使用 [CC BY-NC-SA 4.0](assets/LICENSE.md)，限非商业用途。素材署名和转换步骤见 [assets/README.md](assets/README.md)，其他依赖见 [THIRD_PARTY.md](THIRD_PARTY.md)。
