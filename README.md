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
bash scripts/idf.sh -p /dev/cu.usbmodem1101 flash
bash scripts/idf.sh -p /dev/cu.usbmodem1101 monitor
```

脚本默认 SDK 在 `~/esp-idf`，可用 `IDF_PATH`、`IDF_PYTHON_ENV_PATH` 覆盖。
串口监视器按 Ctrl+] 退出。USB 串口名可能改变。
板载 Flash 实测 16 MiB，固件使用 8 MiB factory 分区，暂未设置 OTA。

屏幕配置在 `menuconfig → DSPet display`：SPI 速度、mode、Y 偏移、反色与 RGB/BGR。
本提交上电循环显示红绿蓝白黑，用于独立检查显示驱动。

## 实现顺序

1. ESP-IDF 工程、接线、ST7789 驱动与色彩验证。
2. 复用 dsh-pet 鲸鱼娘动画，添加来源、许可和可复现的素材转换流程。
3. 在屏幕上循环播放角色待机动画。

本阶段不接入网络、模型或音频。麦克风、喇叭留待后续逐步实现。
