# Agent Orb AI 交接文档

更新时间：2026-08-20

## 交接结论

当前已经真实跑通：

```text
浏览器 / HTTP 查询
        ↓
Agent Orb Gateway
        ↓（Bearer Token）
Snoopy Agent HTTP API
        ↓
Agent Orb 状态机
        ↓ Wi-Fi
DFRobot DFR1221（ESP32-S3）
```

同时验证了反向授权动作：Gateway 发出带 `request_id` 的授权请求，设备收到 `APPROVAL`，设备通过串口发送 `approve` 后，Gateway 进入 `ANSWER` 并保留关联 ID。

必须准确理解当前完成度：DFR1221 的 ST77916 实体圆屏、网络、状态协议、PSRAM、PDM 麦克风和 ESP-SR WakeNet9 模型加载已在真机验证。最新固件启动后主动上报 `idle / Agent Orb / Say Hi ESP`，证明麦克风和唤醒引擎创建成功；设备同时持续拉取 Gateway 状态。已实现最长 8 秒 WAV 录音、轻量 VAD、音频上传和电脑端 whisper.cpp STT。因操作者不在设备旁，这一版还没有真实喊 `Hi ESP` 或录一句中文的端到端声学验证，不要宣称这部分已完成。屏幕上一版已稳定显示蓝底、状态圆环和英文文字；最新固件的最终外观也需回到设备旁确认。当前固件没有覆盖任意中文的字库，中文消息会缺字；CST816S 已初始化，但尚未接入 Orb 确认/拒绝业务交互。

## 相关项目

本仓库的真实 Agent 是同级目录中的 `../snoopy_agent`。Snoopy 生产服务由 macOS LaunchAgent `com.snoopy.agent` 运行，对外提供受 Bearer Token 保护的 `/v1/chat`。

不要把 Snoopy 代码复制进本仓库。Agent Orb 只通过环境变量连接它：

- `SNOOPY_SERVER_URL`
- `SNOOPY_SERVER_TOKEN`

两个变量都没有配置时，Gateway 使用原有确定性 `LocalAssistant`；只配置其中一个时会拒绝启动，防止误回退成演示回答。

## 本次实现

### Gateway → Snoopy

- `src/orb_gateway/assistant.py`
  - 新增 `AssistantBackend` 协议。
  - 新增带认证的 `SnoopyAssistant`。
  - 将 `/v1/chat` 的 `response` 映射成 Orb answer。
  - 上游异常统一转换成 `AssistantUnavailable`。
- `src/orb_gateway/app.py`
  - 根据环境创建真实或演示 Assistant。
  - Snoopy 不可用时返回 HTTP 502，并让设备进入 `error`，不再卡在 `thinking`。
  - 支持测试注入 Assistant。
- `tests/`
  - 覆盖环境配置、Snoopy 响应映射和失败状态回滚。

### DFR1221 固件

- 将构建环境锁定到 pioarduino / Arduino-ESP32 3.3.11。3.0.7 虽能显示界面，但真机出现彩条覆盖；升级后彩条消失。
- 锁定 `ESP32_Display_Panel` 0.1.4、`ESP32_IO_Expander` 0.0.2 和 LVGL 8.3.11。
- 屏幕底层移植自同级已验证项目 `../code_cost`：使用其 DFR1221 引脚、ST77916 厂商初始化指令、13-bit LEDC 背光、72 行内部 RAM 绘制缓冲和 CST816S 初始化。
- 新增圆屏 Orb UI，状态使用独立颜色；当前使用 Montserrat 14/16 英文字体。
- 显存刷新采用异步 QSPI 传输，并在 DMA 完成回调中通知 LVGL。
- 使用 ESP-IDF legacy I2S 驱动初始化 GPIO45/46 PDM 麦克风。
- 适配 Arduino-ESP32 3.x 的 I2S MCLK 和采样位数枚举。
- 修复 ArduinoJson 7 与 `StringSumHelper` 默认值表达式的编译错误。
- 增加 `qio_opi` 内存模式，使板载 8MB OPI PSRAM 正确工作。
- 启动日志会明确打印 PSRAM 可用状态和容量。
- 使用 ESP-SR WakeNet9 内置模型离线检测 `Hi ESP`；BOOT 键仍可按住录音作为备用。
- 官方 `esp_sr_16.csv` 分区表包含 `model` 分区，构建脚本会将框架内的 `srmodels.bin` 加入构建和烧录。
- WakeNet 唤醒后采集 16kHz/16-bit/mono PCM；约 900ms 静音后自动结束，3.5 秒未说话则取消。
- Gateway 新增 `/audio`，校验 WAV 后调用 whisper.cpp，将转写文字复用现有 Snoopy/query 链路。

## 真机实测事实

- USB：Espressif USB JTAG/serial debug unit。
- 芯片：ESP32-S3 revision v0.2。
- Flash：16MB Quad Flash。
- PSRAM：8MB OPI，启动实测 `8386295 bytes` 可用。
- 麦克风：PDM，CLK=GPIO45、DATA=GPIO46，16kHz mono；初始化成功。
- 音频电源：GPIO48。
- 屏幕：ST77916，360×360，50MHz QSPI；SCK=9、CS=10、D0–D3=11–14、背光=15、复位=47。
- 图形：LVGL 8.3.11，72 行内部 RAM 绘制缓冲，`LV_COLOR_16_SWAP=1`。
- 刷新：Arduino-ESP32 3.3.11 + ST77916 自定义初始化表 + DMA 完成回调；实机已确认蓝底文字稳定且无彩条。
- Wi-Fi：同名双频网络可用，ESP32-S3 会连接 2.4GHz。
- 离线唤醒：WakeNet9 内置 `Hi ESP` 模型已写入 `0xC10000`，烧录校验通过。
- 最新固件启动后设备主动上报 `Say Hi ESP`，最终烧录后 Gateway 实测收到 revision 2（2026-08-20 11:06:38 +08:00）。
- Gateway 地址保存在本机 `include/secrets.h`；该文件被 Git 忽略。
- USB 串口编号会在重新插拔后变化，例如 `usbmodem21101`、`usbmodem21201`，烧录前必须重新发现，不能硬编码旧端口。

最终真机测试观察到：

```text
[display] code_cost ST77916 + LVGL ready
[voice] PDM microphone ready (CLK=45 DATA=46, 16 kHz mono)
[voice] WakeNet ready: Hi ESP (... samples/frame)
[wifi] connected, IP=192.168.1.18

IDLE
Agent Orb
```

排障事实：最初的硬件彩条测试能显示，但进入 LVGL 后黑屏，证明屏幕、背光和 QSPI 物理链路正常。移植 `code_cost` 的自定义 ST77916 初始化表与异步刷新回调后，蓝底文字出现但仍有彩条覆盖；再将 Arduino-ESP32 从 3.0.7 升至 3.3.11 后彩条消失。因此不要回退核心版本，也不要把异步回调改回未验证的同步刷新实现。

## 验证命令

仓库根目录：

```bash
PYTHONPATH=src python3 -m unittest discover -s tests -v
```

固件构建：

```bash
cd firmware/agent-orb-dfr1221
../../.venv/bin/pio run
```

发现当前串口后烧录：

```bash
ls /dev/cu.usbmodem*
../../.venv/bin/pio run -t upload --upload-port /dev/cu.usbmodemXXXXX
```

启动连接 Snoopy 与本地 STT 的 Gateway：

```bash
export SNOOPY_SERVER_URL="http://<snoopy-host>:4317"
export SNOOPY_SERVER_TOKEN="<从 Keychain 或安全环境注入>"
export ORB_WHISPER_MODEL="/path/to/ggml-base.bin"
export ORB_WHISPER_LANGUAGE="zh"
PYTHONPATH=src python3 -m orb_gateway --host <本机 Wi-Fi IP> --port 8787 --verbose
```

浏览器模拟器：<http://localhost:8787>

## 本机配置与安全边界

- `firmware/agent-orb-dfr1221/include/secrets.h` 包含 Wi-Fi 凭据，只能留在本机；`.gitignore` 已覆盖它。
- Snoopy Token 保存在 macOS Keychain，不得写进代码、文档、日志或 Git。
- Gateway 目前没有自身认证且 CORS 为 `*`。只监听需要的本机 Wi-Fi IP，不要监听 `0.0.0.0` 或暴露到公网。
- whisper.cpp 模型不提交 Git。本机已安装 `/opt/homebrew/bin/whisper-cli`，模型在 `/Users/vae/Library/Caches/agent-orb/ggml-base.bin`。
- 本地 `.venv/`、PlatformIO `.pio/` 和固件 secrets 都不能提交。
- `../snoopy_agent` 在本次工作开始前已有用户未提交改动；本次没有修改那个仓库。

## 已知缺口与建议顺序

1. **完成声学验收**：人在设备旁先喊 `Hi ESP`，观察进入 Listening，再说一句中文，确认 VAD、上传、Whisper 转写、Snoopy 回答和屏幕状态。如果环境差异导致 VAD 门限不合适，优先换用 ESP-SR AFE/VAD。
2. **评估自定义唤醒词**：当前是官方内置 `Hi ESP`。`Agent Orb` 或 `Snoopy` 需要训练与替换模型，不是改一个字符串。
3. **接入触摸交互**：CST816S 已按 SCL=8、SDA=7、INT=41、RST=40 初始化，把触摸事件映射成屏幕确认/拒绝动作并接到 `GatewayClient`。
4. **补齐中文字库**：根据实际 UI 文案生成 LVGL 字体子集，或引入可覆盖动态回答的中文字库；注意内部 RAM 和 Flash 占用。当前不要宣称任意中文已可显示。
5. **对接 Snoopy 记忆例外审核**：当前 `SnoopyAssistant` 只在标题提示待审核候选数量，Orb 的确认动作还没有调用 Snoopy candidate accept/reject API。
6. **使用长轮询**：固件目前每 800ms 请求 `/state`；Gateway 已有 `/events?after=N&timeout=20`。
7. **持久运行 Gateway**：当前由终端手动启动，后续可增加独立 LaunchAgent，并继续从 Keychain 注入 Token。
8. **加强 Gateway 安全**：如果跨可信单机/局域网使用，增加认证、来源限制和更窄的 CORS。

## 下一位 AI 的开始方式

1. 先读 `README.md`、`docs/protocol.md` 和本文。
2. 执行 `git status`，保留用户新增改动。
3. 确认 `secrets.h` 存在但被 Git 忽略，不要读取或输出其值。
4. 运行 Python 测试和固件构建。
5. 屏幕已按 `../code_cost` 的已验证底层实现并烧录验证。保持 Arduino-ESP32 3.3.11、LVGL 8.3.11 和当前 ST77916 初始化/刷新实现；如要升级或改动，必须重新验证无黑屏、无彩条，并同时检查 PDM 麦克风和 Wi-Fi。
6. 人回到设备旁后，先从 `Hi ESP` 实声唤醒测试开始；这是当前唯一没有真机完成的关键闭环。
