# 鲸鱼娘待机素材

复用 [PC2005-cloud/dsh-pet](https://github.com/PC2005-cloud/dsh-pet) 的以下动画，
固定来源提交 `fb38ce620576f5ed3735a99536d2b1a4a9eb313f`。
各个 `source/*.json` 记录原路径、SHA-256、作者和许可，原 WebM 一并保留以便复现。

| 标识 | 上游动作 |
| --- | --- |
| idle | 待机呼吸休闲 |
| wave | 点击回应-元气挥手 |
| stretch | 超大伸懒腰 |
| dance | 轻快摇摆舞 |
| eat | 吃白饭 |
| sleep | 睡觉-循环 |

这不是 DeepSeek 官方形象。动画来源为 PC2005-cloud/dsh-pet；
角色原型「溟月」为 B 站 [上善无形](https://space.bilibili.com/4456176) 创作，
DeepSeek 版女仆鲸鱼娘设计为 [ZipZipPipe](https://space.bilibili.com/4168597) 创作。

源素材及本目录中转换后的衍生素材遵循 **CC BY-NC-SA 4.0**，
须署名、非商业使用、按相同方式共享。
请保留本说明、来源 manifest 和 [许可声明](LICENSE.md)。

## 转换

```sh
python3 -m venv .venv
.venv/bin/pip install -r tools/requirements.txt
.venv/bin/python tools/convert_animation.py
```

还需要安装带 `libvpx-vp9` 解码器的 ffmpeg。
源文件 SHA-256 校验通过才转换；显式选用能保留透明通道的解码器。
对整段动画统一裁剪，保持动作位置与缩放稳定；透明区域合成浅蓝背景。

输出：

- `generated/<标识>.rgb565z`：240×240，24 fps，共用 128 色调色板，索引差分后 zlib 压缩。
- `generated/<标识>.json`：尺寸、帧数、帧率、固定裁剪范围和数据校验值。
- `generated/<标识>-preview.png` / `.gif`：主机预览。

帧索引在 JSON 的 `offsets` 中，最后一项为二进制文件长度。
解压后每帧为 512 字节的 RGB565_BE 调色板 + 57,600 字节的索引数据。
每 24 帧是完整关键帧，其余索引与前一帧 XOR；调色板在每段内固定，减少颜色闪动。
从最近关键帧依次恢复索引，再在同一解压缓冲内展开完整 RGB565 发送给屏幕。
板端使用 ESP32-S3 ROM 自带 miniz 解压，不需要联网或在 ESP32 上播放 WebM。
完整帧大小为 115,200 字节；固件保留一帧解压缓冲与一帧 DMA 缓冲，
另保留 57,600 字节前帧索引；压缩帧常驻 Flash。
调色板量化会改变部分颜色；差分和 zlib 压缩本身无损。
每帧统一裁剪位置与大小，不使用随机抖动。预览显示的是量化后的实际颜色。
