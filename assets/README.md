# 鲸鱼娘待机素材

复用 [PC2005-cloud/dsh-pet](https://github.com/PC2005-cloud/dsh-pet) 的以下动画，
固定来源提交 `fb38ce620576f5ed3735a99536d2b1a4a9eb313f`。
各个 `source/*.json` 记录原路径、SHA-256、作者和许可，原 WebM 一并保留以便复现。

| 标识 | 上游动作 |
| --- | --- |
| idle | 待机呼吸休闲 |
| wave | 点击回应-元气挥手 |
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

- `generated/<标识>.rgb565z`：240×240，6 fps，每帧 RGB565（高字节在前）分别 zlib 压缩。
- `generated/<标识>.json`：尺寸、帧数、帧率、固定裁剪范围和数据校验值。
- `generated/<标识>-preview.png` / `.gif`：主机预览。

帧索引在 JSON 的 `offsets` 中，最后一项为二进制文件长度。
每帧独立压缩，可从任意帧开始播放，不需要重新解码整个片段。
板端使用 ESP32-S3 ROM 自带 miniz 解压，不需要联网或在 ESP32 上播放 WebM。
完整帧大小为 115,200 字节；固件保留一帧解压缓冲与一帧 DMA 缓冲，
压缩帧常驻 Flash。待机片段从 6.6 MiB 降为约 2 MiB，解压后像素完全一致。
