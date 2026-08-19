# DFR1221 firmware

这是 Agent Orb 的 DFRobot DFR1221 真机固件。

## 当前可验证的能力

- ESP32-S3 连接 Wi-Fi
- 从 Local Gateway 拉取 Orb 状态
- 将确认、拒绝、唤醒等操作发回 Gateway
- 驱动 360×360 ST77916 圆屏，展示 Orb 状态、标题和英文消息
- 初始化 GPIO45/46 的 16kHz 单声道 PDM 麦克风
- 通过串口镜像状态和启动诊断

## 配置

```bash
cp include/secrets.example.h include/secrets.h
```

编辑 `include/secrets.h`，填入 Wi-Fi 和运行 Gateway 的电脑局域网地址。

## 构建与烧录

仓库已锁定 Arduino-ESP32 3.3.11、ESP32_Display_Panel 0.1.4 和 LVGL 8.3.11。安装 PlatformIO 后：

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

## 屏幕实现

`OrbDisplay` 使用从同级 `code_cost` 项目验证过的引脚、ST77916 厂商初始化表和异步 DMA 刷新回调，通过 LVGL 8.3.11 刷新 UI。背光为 GPIO15，复位为 GPIO47，QSPI 为 GPIO9–14 和 GPIO10 片选。当前只启用 Montserrat 英文字体；动态中文回答需要后续补充中文字库。CST816S 已初始化，但尚未接入 Orb 业务交互。

## WakeNet 接入点

板载麦克风已经在 `OrbVoice` 中初始化。下一步用 ESP-IDF/ESP-SR 的 AFE 接管这段 PCM 流：

1. WakeNet 检测成功，发送 `wake`。
2. AFE/VAD 检测到语音结束，上传录音并发送 `speech_end`。
3. STT、LLM、Agent 工具调用留在电脑端。
