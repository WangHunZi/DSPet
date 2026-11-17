# 2026-11-17 实物验证

验证固件提交：`3a282f4`（六种动作自动轮换与 BOOT 手动切换）。
平台：XIAO ESP32S3 Plus，ESP-IDF v6.0.2，CPU 240 MHz，ST7789 240×240。
接线见项目 README；SPI 40 MHz、mode 0、Y 偏移 0。

## 功能验收

- 六段动作：待机、挥手、伸懒腰、跳舞、吃饭、睡觉。
- 每段保留原素材 24 fps，241 帧，约 10 秒；播完自动进入下一段。
- 短按 BOOT 可提前切换；按键与到期同时发生时只前进一段。
- 用户实物反馈：「自动切换正常，也更流畅」。
- 当前硬件没有接入触摸输入，交互采用 BOOT。

## 播放性能

以下为固件日志样本。fps 为主机收到的固件统计，不是对屏幕光学拍摄测量；
SPI 耗时包含命令、DMA 发送及等待完成，解码包含差分恢复和颜色展开。

| 动作 / 样本 | 实测发送 fps | 平均解码 | 平均 SPI |
| --- | --- | --- | --- |
| 伸懒腰 | 23.9 | 14.35 ms | 24.04 ms |
| 跳舞 / 1 | 23.7 | 15.05 ms | 24.13 ms |
| 跳舞 / 2 | 23.8 | 15.03 ms | 24.14 ms |
| 跳舞 / 3 | 23.9 | 14.96 ms | 24.11 ms |

24 fps 的每帧预算约 41.7 ms。样本中的解码与发送合计约 38～39 ms。
当前瓶颈是解码与整帧传输的总耗时；12 fps 并非四线 SPI 的硬件上限。
对 115,200 字节的 RGB565 全帧，40 MHz 单数据线的理想传输时间约 23.0 ms。

日志摘录：

```text
I (28198) pet: stretch frame=217/241 fps=23.9 decode=14.35ms SPI=24.04ms
I (29162) pet: Action: dance (BOOT presses=2)
I (32162) pet: dance frame=71/241 fps=23.7 decode=15.05ms SPI=24.13ms
I (35162) pet: dance frame=143/241 fps=23.8 decode=15.03ms SPI=24.14ms
I (38200) pet: dance frame=216/241 fps=23.9 decode=14.96ms SPI=24.11ms
I (39204) pet: Action: eat (BOOT presses=2)
I (39746) pet: Action: sleep (BOOT presses=3)
I (40830) pet: Action: idle (BOOT presses=4)
I (41622) pet: Action: wave (BOOT presses=5)
I (42164) pet: Action: stretch (BOOT presses=6)
I (43581) pet: Action: dance (BOOT presses=7)
```

## 构建与资源

- 10 项主机测试通过，包含调色板展开、差分与关键帧恢复、
  BOOT 消抖、长按与计数回绕、自动/手动切换重合。
- 固件构建通过；镜像 13,884,768 字节，位于 15 MiB factory 分区内。
- 烧录完成并通过 Flash hash 校验。
- 不启用 PSRAM。主要图像缓冲为 115,200 字节 DMA、115,200 字节解压/展开、
  57,600 字节前帧索引；解压器状态独立分配在堆上。
- 为容纳多段 24 fps 动画，使用每段共用 128 色调色板与差分压缩。
  色彩量化会改变部分颜色；差分与 zlib 压缩本身无损。

本次先遇到 USB 自动下载无响应，用户拔插并按 BOOT/RESET 进入下载模式后，
以 `--before no-reset` 完成烧录。读取应用日志时使用 DTR=1、RTS=0。
