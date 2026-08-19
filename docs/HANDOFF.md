# Agent Orb AI 交接文档

更新时间：2026-08-19

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

必须准确理解当前完成度：DFR1221 的 ST77916 实体圆屏、网络、状态协议、PSRAM 和麦克风初始化已在真机验证。屏幕可显示状态、标题、中英文消息和授权提示；串口仍保留为诊断镜像。麦克风只完成了 PDM 初始化，尚无 WakeNet、录音、VAD、音频上传或 STT；CST816S 触摸也尚未接入交互。

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

- 将构建环境锁定到 pioarduino / Arduino-ESP32 3.0.7，满足 DFRobot 官方屏幕示例的 3.0.1+ 要求。
- 锁定 `ESP32_Display_Panel` 0.1.4、`ESP32_IO_Expander` 0.0.2 和 LVGL 8.4.0。
- 根据官方 DFR1221 引脚实现 360×360 ST77916 QSPI 屏、GPIO15 背光和 GPIO47 复位。
- 新增圆屏 Orb UI，状态使用独立颜色，并启用 LVGL 常用中文字库。
- 显存刷新采用 DMA 完成后同步通知 LVGL，避免首帧阻塞。
- 使用 ESP-IDF legacy I2S 驱动初始化 GPIO45/46 PDM 麦克风。
- 适配 Arduino-ESP32 3.x 的 I2S MCLK 和采样位数枚举。
- 修复 ArduinoJson 7 与 `StringSumHelper` 默认值表达式的编译错误。
- 增加 `qio_opi` 内存模式，使板载 8MB OPI PSRAM 正确工作。
- 启动日志会明确打印 PSRAM 可用状态和容量。

## 真机实测事实

- USB：Espressif USB JTAG/serial debug unit。
- 芯片：ESP32-S3 revision v0.2。
- Flash：16MB Quad Flash。
- PSRAM：8MB OPI，启动实测 `8386295 bytes` 可用。
- 麦克风：PDM，CLK=GPIO45、DATA=GPIO46，16kHz mono；初始化成功。
- 音频电源：GPIO48。
- 屏幕：ST77916，360×360，50MHz QSPI；SCK=9、CS=10、D0–D3=11–14、背光=15、复位=47。
- 图形：LVGL 8.4，48 行内部 RAM 绘制缓冲，常用中文字体已启用。
- Wi-Fi：同名双频网络可用，ESP32-S3 会连接 2.4GHz。
- Gateway 地址保存在本机 `include/secrets.h`；该文件被 Git 忽略。
- USB 串口编号会在重新插拔后变化，例如 `usbmodem21101`、`usbmodem21201`，烧录前必须重新发现，不能硬编码旧端口。

最终真机测试观察到：

```text
[display] ST77916 + LVGL ready
[voice] PDM microphone ready (CLK=45 DATA=46, 16 kHz mono)
[wifi] connected, IP=192.168.1.18

THINKING
正在思考

ANSWER
Snoopy
真机链路完成。
```

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

启动连接 Snoopy 的 Gateway：

```bash
export SNOOPY_SERVER_URL="http://<snoopy-host>:4317"
export SNOOPY_SERVER_TOKEN="<从 Keychain 或安全环境注入>"
PYTHONPATH=src python3 -m orb_gateway --host 0.0.0.0 --port 8787 --verbose
```

浏览器模拟器：<http://localhost:8787>

## 本机配置与安全边界

- `firmware/agent-orb-dfr1221/include/secrets.h` 包含 Wi-Fi 凭据，只能留在本机；`.gitignore` 已覆盖它。
- Snoopy Token 保存在 macOS Keychain，不得写进代码、文档、日志或 Git。
- Gateway 目前没有自身认证且 CORS 为 `*`。监听 `0.0.0.0` 只适合可信局域网，不可直接暴露公网。
- 本地 `.venv/`、PlatformIO `.pio/` 和固件 secrets 都不能提交。
- `../snoopy_agent` 在本次工作开始前已有用户未提交改动；本次没有修改那个仓库。

## 已知缺口与建议顺序

1. **建立真实语音输入**：读取已经初始化的 PDM PCM，接 WakeNet/VAD，再把录音上传电脑端进行 STT。语义理解继续留在 Snoopy/Gateway，不放到 ESP32。
2. **接入触摸交互**：使用官方 CST816S 引脚（SCL=8、SDA=7、INT=41、RST=40），把屏幕确认/拒绝动作接到 `GatewayClient`。
3. **对接 Snoopy 记忆例外审核**：当前 `SnoopyAssistant` 只在标题提示待审核候选数量，Orb 的确认动作还没有调用 Snoopy candidate accept/reject API。
4. **使用长轮询**：固件目前每 800ms 请求 `/state`；Gateway 已有 `/events?after=N&timeout=20`。
5. **持久运行 Gateway**：当前由终端手动启动，后续可增加独立 LaunchAgent，并继续从 Keychain 注入 Token。
6. **加强 Gateway 安全**：如果跨可信单机/局域网使用，增加认证、来源限制和更窄的 CORS。

## 下一位 AI 的开始方式

1. 先读 `README.md`、`docs/protocol.md` 和本文。
2. 执行 `git status`，保留用户新增改动。
3. 确认 `secrets.h` 存在但被 Git 忽略，不要读取或输出其值。
4. 运行 Python 测试和固件构建。
5. 屏幕已按 DFRobot 官方 DFR1221 示例实现并烧录验证。如要升级核心或屏幕库，先验证 ST77916、PDM 麦克风和 Wi-Fi 三者兼容性。
