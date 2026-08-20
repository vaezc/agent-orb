# Agent Orb

Agent Orb 是为 DFRobot DFR1221（ESP32-S3、360×360 圆屏）设计的桌面 AI 实体入口。

当前版本先跑通最重要的闭环：

```text
电脑 / Agent 发起事件
        ↓
Local Gateway（本项目）
        ↓ Wi-Fi
DFR1221 / 浏览器模拟器
        ↓
用户确认、取消或语音唤醒
        ↓
结果返回电脑 / Agent
```

## 已完成

- 明确的 Orb 状态机：`Idle → Listening → Thinking → Answer`
- 主动提醒与人工授权：`Attention → Approval → Answer`
- 零第三方依赖的本地 HTTP Gateway
- 360×360 圆屏风格浏览器模拟器
- Web 文字输入与浏览器原生中文语音识别
- 可替换的本地助手引擎和 4 个演示工具
- DFR1221 真机固件、ST77916 圆屏 UI 与设备协议客户端
- ESP-SR WakeNet9 离线 `Hi ESP` 唤醒、轻量 VAD 与 BOOT 按键录音备用
- 设备 WAV 上传、电脑端 whisper.cpp 转写和 Snoopy 查询链路
- 自动测试

## 立即运行

只需要 Python 3.10+：

```bash
python3 -m orb_gateway --host 0.0.0.0 --port 8787
```

如果还没有安装本项目，直接在仓库根目录运行：

```bash
PYTHONPATH=src python3 -m orb_gateway --host 0.0.0.0 --port 8787
```

然后打开 <http://localhost:8787>。页面本身就是一个可交互的 Orb 模拟器。

可以直接输入“现在几点”“系统状态”或“你会做什么”。Chrome/Edge 在 localhost 下还可以点击麦克风按钮，使用浏览器原生语音识别跑通 `Listening → Thinking → Answer`。当前不会把未知问题伪装成 AI 回答，而会明确提示真实 LLM 尚未接入。

### macOS 常驻运行

真机使用时可安装 `deploy/macos/com.agent-orb.gateway.plist` 为用户级
LaunchAgent。它会在登录后启动、异常退出后重启，并且：

- 从 macOS Keychain 的 `snoopy-server-token` 读取 Token；
- 从独立的 `agent-orb-gateway-token` 读取设备认证 Token；
- 从 Snoopy 生产配置读取 Agent 地址；
- 仅在 plist 配置的 Wi-Fi IP 上监听 8787，不暴露到其他网络接口；
- 使用 `$HOME/Library/Caches/agent-orb/ggml-base.bin` 做本地 STT。

服务运行副本位于 `$HOME/.agent-orb/`，避免 macOS 对后台进程访问
`Documents` 的权限限制；日志位于 `$HOME/Library/Logs/AgentOrb/`。
DHCP 导致本机 Wi-Fi IP 改变时，需同步更新 plist 的 `ORB_GATEWAY_HOST`
和固件 `ORB_GATEWAY_URL`。
首次安装前运行 `python3 scripts/provision_gateway_token.py`，它会将同一个
随机 Token 安全写入 Keychain 和被 Git 忽略的固件头文件。
如需轮换已配置的 Token，运行
`python3 scripts/provision_gateway_token.py --rotate`，然后重启 Gateway 并重新烧录固件。

### 接入 Snoopy Agent

如果同一台电脑上已经运行 Snoopy Server，可以让 Gateway 把查询交给真实 Agent：

```bash
export SNOOPY_SERVER_URL="http://127.0.0.1:4317"
export SNOOPY_SERVER_TOKEN="<Snoopy Server Token>"
PYTHONPATH=src python3 -m orb_gateway --host 0.0.0.0 --port 8787
```

两个变量都未设置时仍使用内置演示助手；只设置其中一个时 Gateway 会拒绝启动，避免悄悄回退到演示回答。Snoopy 不可用时 Orb 会进入 `error`，而不是一直停在 `thinking`。

## 试一下完整链路

在另一个终端发送主动提醒：

```bash
curl -X POST http://localhost:8787/api/v1/devices/demo/actions \
  -H 'Content-Type: application/json' \
  -d '{"action":"attention","title":"需要关注","message":"15 分钟后有项目会议"}'
```

发送授权请求：

```bash
curl -X POST http://localhost:8787/api/v1/devices/demo/actions \
  -H 'Content-Type: application/json' \
  -d '{"action":"request_approval","title":"部署生产环境？","message":"commit a82f31"}'
```

查询设备状态：

```bash
curl http://localhost:8787/api/v1/devices/demo/state
```

## API

### `GET /api/v1/devices/{device_id}/state`

返回当前状态、展示内容和递增版本号。

### `POST /api/v1/devices/{device_id}/actions`

请求体：

```json
{
  "action": "attention",
  "title": "需要关注",
  "message": "15 分钟后有会议",
  "request_id": "optional-correlation-id"
}
```

支持的动作：

- `wake`
- `speech_end`
- `answer`
- `attention`
- `request_approval`
- `approve`
- `reject`
- `dismiss`
- `cancel`
- `fail`
- `reset`

非法的状态跳转会返回 HTTP 409，而不会悄悄破坏设备状态。


### `POST /api/v1/devices/{device_id}/query`

提交 Web 文字或浏览器语音识别结果：

```json
{"text":"现在几点？","source":"web"}
```

Gateway 自动完成状态流转，并返回最终快照以及实际调用的本地工具名。

### `POST /api/v1/devices/{device_id}/audio`

接收设备上传的 `audio/wav`（16kHz、16-bit、单声道，最长 10 秒）。
配置 `ORB_WHISPER_MODEL` 后，Gateway 使用本地 whisper.cpp 转写，再将文字
交给当前 Assistant：

```bash
export ORB_WHISPER_MODEL="/path/to/ggml-base.bin"
export ORB_WHISPER_LANGUAGE="zh"
```

### `GET /api/v1/tools`

列出当前助手引擎真正实现的工具。现阶段包括时间日期、Gateway 状态、能力说明和安全回显。

### `GET /api/v1/devices/{device_id}/events?after={revision}&timeout=20`

长轮询接口。状态版本大于 `after` 时立即返回，否则等待，最长 25 秒。ESP32 可以用它减少空轮询。

## 固件

固件位于 [`firmware/agent-orb-dfr1221`](firmware/agent-orb-dfr1221)。它已包含：

- DFR1221 的 ESP32-S3/16MB Flash/8MB PSRAM 构建设置
- Wi-Fi 和 Gateway 通信
- 与电脑端一致的状态模型
- 经 `code_cost` 真机验证的 ST77916 QSPI 圆屏驱动和 LVGL 8.3.11 UI
- 英文状态文字与状态颜色反馈（动态中文字库待补）
- GPIO45/46 PDM 麦克风采集与 PSRAM WAV 录音缓冲
- 官方 ESP-SR AFE + WakeNet9 离线 `Hi ESP` 唤醒、轻量 VAD 和 BOOT 按键备用
- 自动将 ESP-SR 模型分区与固件一起烧录

屏幕、PSRAM、PDM 麦克风、Wi-Fi、WakeNet 模型加载和 Gateway 状态拉取
已在实际 DFR1221 上通过。远程串口触发录音后，真机已完成 8 秒 WAV 上传、
whisper.cpp 转写、Snoopy Agent 查询和屏幕答案回显；实测录音峰值均幅为 1407。
设备自检状态为 `Say Hi ESP`。真人近距离说 `Hi ESP` 已触发 wake、录音、
转写和 Snoopy 回答；当前需要继续增强唤醒后的视觉反馈，并改善现场 VAD 与
转写准确度。

## 测试

```bash
PYTHONPATH=src python3 -m unittest discover -s tests -v
```

## 项目结构

```text
src/orb_gateway/                Local Gateway
src/orb_gateway/static/         浏览器 Orb 模拟器
firmware/agent-orb-dfr1221/     ESP32-S3 固件骨架
tests/                          状态机和 HTTP 测试
docs/protocol.md                设备协议
docs/HANDOFF.md                 真机验证与后续 AI 交接
```
