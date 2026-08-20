# DFR1221 firmware

这是 Agent Orb 的 DFRobot DFR1221 真机固件。

## 当前可验证的能力

- ESP32-S3 连接 Wi-Fi
- 从 Local Gateway 拉取 Orb 状态
- 将确认、拒绝、唤醒等操作发回 Gateway
- 驱动 360×360 ST77916 圆屏，展示 Orb 状态、标题和英文消息
- 初始化 GPIO45/46 的 16kHz 单声道 PDM 麦克风
- 按住 BOOT 键录音、松开后上传 WAV 到 Gateway
- 使用 ESP-SR WakeNet9 离线识别 `Hi ESP`，并在说完后自动上传
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

真机语音测试：按住设备 BOOT 键说话，松开后设备会上传最长 8 秒的
16kHz/16-bit 单声道 WAV。Gateway 配置本地 Whisper 后会把转写文字直接
交给当前 Assistant。

免按键语音测试：先说 `Hi ESP`，设备进入 `LISTENING` 后直接说问题。
简单 VAD 检测到约 900ms 静音后自动上传；若 3.5 秒内没有说话则取消。
官方模型与固件一起烧录到 `model` 分区，烧录命令无需额外步骤。
启动成功后固件会向 Gateway 上报 `Say Hi ESP`；如模型加载失败，
设备则进入 `error` 并显示 `Wake word unavailable`。

Gateway 需要先安装 whisper.cpp 并配置模型：

```bash
export ORB_WHISPER_MODEL="/path/to/ggml-base.bin"
export ORB_WHISPER_LANGUAGE="zh"
```

## 屏幕实现

`OrbDisplay` 使用从同级 `code_cost` 项目验证过的引脚、ST77916 厂商初始化表和异步 DMA 刷新回调，通过 LVGL 8.3.11 刷新 UI。背光为 GPIO15，复位为 GPIO47，QSPI 为 GPIO9–14 和 GPIO10 片选。当前只启用 Montserrat 英文字体；动态中文回答需要后续补充中文字库。CST816S 已初始化，但尚未接入 Orb 业务交互。

## WakeNet 接入点

板载麦克风已经在 `OrbVoice` 中采集 PCM，WakeNet9 已负责 `Hi ESP`
离线唤醒，BOOT 键作为备用。当前说完检测是基于环境噪声基线的轻量 VAD；
后续可接 ESP-SR AFE VAD：

1. WakeNet 检测成功，发送 `wake`。
2. AFE/VAD 检测到语音结束，上传录音并发送 `speech_end`。
3. STT、LLM、Agent 工具调用留在电脑端。
