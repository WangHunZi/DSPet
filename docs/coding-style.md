# DSPet 代码风格

本规则适用于 DSPet 新增的固件 C 代码、对应的主机 C 测试和文档中的 C 示例。
命名和注释沿用 UartProto 的思路，公开符号使用 `DSPet` 前缀。
给现有模块补功能时，沿用该模块的既有命名。需要统一命名时，在对应模块的重构中一并更新声明、调用方和测试。

## 实现语言与调用边界

- 固件实现使用 `.c` 文件，头文件保持 C 兼容并直接包含声明所需的依赖。ESP-IDF 入口保留 `app_main`。SDK、FreeRTOS、第三方接口和构建生成的符号沿用原名。
- 跨模块接口在头文件中声明。仅在本文件使用的函数和对象按需声明为 `static`。
- 按键消抖、动作状态和帧解码等纯逻辑继续支持主机 C11 测试。GPIO、I²S、SPI DMA 和 FreeRTOS 调用留在需要硬件或任务上下文的模块。
- 头文件需要供 C++ 调用时，在 C 接口声明外使用 `__cplusplus` 条件下的 `extern "C"`。

## 命名

| 类别 | 规则 | 示例 |
| --- | --- | --- |
| 局部变量、函数参数 | 小驼峰 | `frameBytes`、`sampleCount`、`timeoutMs` |
| 结构体、联合体及函数指针成员 | 小驼峰 | `dmaBuffer`、`frameCount`、`onComplete` |
| 文件内部函数 | 小驼峰 | `sendFrame()`、`restoreDelta()` |
| 跨模块函数 | `DSPet` + 模块名 + 动作，连续大驼峰 | `DSPetDisplayPresent()`、`DSPetIndexedFrameExpand()` |
| 跨模块类型及回调类型 | `DSPet` + 模块名和用途，连续大驼峰 | `DSPetButtonFilter`、`DSPetDisplayCompletionHandler` |
| 命名常量 | `k` + 大驼峰，跨模块常量带 `DSPet` 和模块限定名 | `kDebounceMs`、`kDSPetDisplayWidthPx` |
| 枚举项 | `k` + 类型限定名 + 大驼峰 | `kDSPetAudioStateIdle`、`kDSPetAudioStateRecording` |
| 静态可变对象 | `s` + 大驼峰 | `sDmaBuffer`、`sTransferDone` |
| 项目宏、头文件保护宏 | 大写下划线，带项目和模块名 | `DSPET_DISPLAY_H` |
| 新模块文件 | 以主要类型或模块的大驼峰名称命名 | `DSPetButtonFilter.c`、`DSPetButtonFilter.h` |

- C 枚举项共享标识符作用域。跨模块枚举项带完整类型限定名，避免 `kIdle`、`kBusy` 这类通用名称。
- `k` 用于命名常量和枚举项。只读参数 `const uint8_t *pixels` 仍叫 `pixels`，实例成员仍用小驼峰。静态常量用 `k`，静态可变对象用 `s`，各自保留所需的 `const` 和 `static` 声明。
- 缩写按单词处理，例如 `txBuffer`、`rxSampleCount`、`dmaBuffer`。产品型号和外部标识保留原有拼写，例如 `ST7789`、`ESP32S3`、`CONFIG_DSPET_AUDIO_CHECK`。
- 时间、容量和计数尽量在名称中标明单位，例如 `timeoutMs`、`elapsedUs`、`capacityBytes`、`sampleCount`。音频接口区分样本数、帧数和字节数。

## 中文 Doxygen 注释

跨模块接口的使用契约写在头文件。函数、回调、结构体和枚举用 `/** ... */` 和 `@brief`，成员和枚举项用 `/**< 中文说明。 */`。

- 每个具名参数使用 `@param[in]`、`@param[out]` 或 `@param[in,out]`。既读又改的缓冲区用 `in,out`，仅由函数填充的输出缓冲区用 `out`。
- 按需说明指针是否可空、长度和单位、可接受的范围，以及资源由谁持有和释放。
- 非 `void` 返回使用 `@return`。返回值有多种含义时，逐项说明触发条件。透传驱动错误时写明错误来源。无参数或无返回值时省略对应标签。
- 用 `@note` 写明任务或 ISR 上下文、阻塞条件、异步完成时机和缓冲区有效期。实现内用中文说明有必要交代的原因，例如为何等待 DMA 完成后才可复用缓冲区。

下面以帧解码接口为例。当前函数声明见 [DSPetIndexedFrame.h](../main/services/animation/DSPetIndexedFrame.h)。

```c
/**
 * @brief 将调色板索引原地展开为高字节在前的 RGB565 像素。
 *
 * @param[in,out] buffer 输入为 512 字节调色板及紧随其后的像素索引，每个索引占 1 字节。
 *                      成功后从缓冲区首字节开始存放输出像素，每个像素占 2 字节。
 * @param[in] pixelCount 待展开的像素数。
 * @param[in] capacityBytes 缓冲区容量，单位为字节，须容纳完整输入和完整输出。
 *
 * @return
 *     - true：展开成功。
 *     - false：buffer 为 NULL，或容量不足以容纳输入或输出。缓冲区内容保持不变。
 */
bool DSPetIndexedFrameExpand(uint8_t *buffer, size_t pixelCount, size_t capacityBytes);
```

## 参数检查责任

- 对外入口检查调用方提供的参数。初始化检查必需的配置和回调，硬件模块负责检查设备状态并处理驱动错误。
- 内部函数依赖调用方已经建立的前置条件。同一条件在所属边界检查一次，调用方根据返回值处理可恢复错误。
- 参数检查和注释一起核对。返回成功的时机要具体，例如 SPI 提交成功、DMA 已完成、I²S 数据已排空分别对应不同事件。

## 格式与审核

- 用四空格缩进，代码每行最多 120 列。函数定义的左花括号另起一行，控制语句的左花括号跟在语句后。相邻的函数、结构体和枚举定义之间留一个空行。
- 格式化限于本次修改的文件。审核时核对命名、中文注释和接口契约。纯风格修改保留原有状态转换、错误返回、数据格式和所有权语义。
- 固件变更按影响范围运行主机构建测试和 ESP-IDF 构建。涉及按键、显示或音频行为时，按 [README](../README.md) 的步骤进行实物验证。

## Git 提交

- 标题沿用 `type(scope): 中文说明`。`type` 和 `scope` 延续仓库已有的英文命名，正文用中文交代改动原因、行为和验证结果。接口名、命令和日志字段保留原拼写。
- 每笔提交对应一项可独立 review 的改动。需要分阶段实现时，按职责拆提交，并随对应改动更新测试或文档。
- 示例 `feat(audio): 对齐麦克风采样与播放参考`、`fix(app): 丢弃已取消回合的迟到音频`。

## 参考

- 公共头文件的 API 注释结构。 [Espressif API 注释格式](https://docs.espressif.com/projects/esp-docs/en/latest/writing-documentation/api-documentation.html)
- 修改现有模块时匹配模块内的风格。 [Matter 编码指南](https://github.com/project-chip/connectedhomeip/blob/master/docs/style/CODING_STYLE_GUIDE.md)
