# 第三方来源

ST7789 驱动使用 ESP-IDF 自带 `esp_lcd`（Apache-2.0）。
附加初始化寄存器值参考 [Waveshare Pico-LCD-1.3](https://github.com/waveshare/Pico_code/blob/main/Python/Pico-LCD-1.3/Pico-LCD-1.3.py)，
沿用 2026-11-16 实物上能够显示红绿蓝、白框和文字的诊断程序。
当时使用四组配置循环，尚未单独确认每组都能正常显示；本工程默认 mode 0、Y 偏移 0。

角色动画来自 [PC2005-cloud/dsh-pet](https://github.com/PC2005-cloud/dsh-pet)。
固定版本、校验值、转换方法及作者见 [assets/README.md](assets/README.md)。
素材按 CC BY-NC-SA 4.0 使用，相关文件不适用上游代码的 MIT 许可。
