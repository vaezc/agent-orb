# DFR1221 firmware

这是 Agent Orb 的 DFRobot DFR1221 固件骨架。

## 当前可验证的能力

- ESP32-S3 连接 Wi-Fi
- 从 Local Gateway 拉取 Orb 状态
- 将确认、拒绝、唤醒等操作发回 Gateway
- 初始化 GPIO45/46 的 16kHz 单声道 PDM 麦克风
- 通过串口呈现状态，验证真实板卡与 Gateway 的完整通信

## 配置

```bash
cp include/secrets.example.h include/secrets.h
```

编辑 `include/secrets.h`，填入 Wi-Fi 和运行 Gateway 的电脑局域网地址。

## 构建与烧录

安装 PlatformIO 后：

```bash
pio run
pio run -t upload
pio device monitor
```

串口测试按键：

| 字符 | 动作 |
|---|---|
| `w` | 模拟 WakeNet 唤醒 |
| `e` | 模拟 VAD 判断说完 |
| `a` | 确认授权 |
| `r` | 拒绝授权 |
| `d` | 关闭消息 |
| `x` | 重置到待机 |

## 为什么屏幕暂时是串口实现

DFRobot 为这块板提供的是配套 `ESP32_Display_Panel`、`ESP32_IO_Expander`、LVGL 8.4 和特定 ST77916 示例包。屏幕时序和 IO 扩展配置必须以官方示例为准。当前代码把显示实现封装在 `OrbDisplay` 中，拿到并验证官方库后只替换这个类。

## WakeNet 接入点

板载麦克风已经在 `OrbVoice` 中初始化。下一步用 ESP-IDF/ESP-SR 的 AFE 接管这段 PCM 流：

1. WakeNet 检测成功，发送 `wake`。
2. AFE/VAD 检测到语音结束，上传录音并发送 `speech_end`。
3. STT、LLM、Agent 工具调用留在电脑端。
