# Agent Orb Device Protocol v0.1

协议刻意保持简单：UTF-8 JSON over HTTP。ESP32 和浏览器模拟器使用同一组接口。

生产 Gateway 设置 `ORB_GATEWAY_TOKEN` 后，除 `/api/v1/health` 外的 API
都需要 `Authorization: Bearer <token>`。设备 Token 由
`scripts/provision_gateway_token.py` 同步到 macOS Keychain 和本地固件头文件，
不得提交 Git 或写入 plist。

## 状态

| 状态 | 含义 | 常见下一步 |
|---|---|---|
| `idle` | 待机 | `wake` / `attention` |
| `listening` | 正在收音 | `speech_end` / `cancel` |
| `thinking` | 电脑端处理中 | `answer` / `request_approval` |
| `answer` | 展示结果 | `dismiss` / `wake` |
| `attention` | AI 主动提醒 | `dismiss` / `wake` |
| `approval` | 等待人工授权 | `approve` / `reject` |
| `error` | 发生错误 | `reset` / `dismiss` |

`attention`、`request_approval`、`fail` 和 `reset` 是全局动作，可从任何状态进入。

## 快照

```json
{
  "device_id": "demo",
  "state": "approval",
  "title": "部署生产环境？",
  "message": "commit a82f31",
  "revision": 7,
  "request_id": "deploy-2026-001",
  "updated_at": "2026-08-18T01:02:03.456+00:00"
}
```

`revision` 在每次成功动作后递增。设备重连后先拉取 `/state`，随后通过 `/events?after=N` 等待更新。

## 关联请求

发送授权请求时可以附带 `request_id`。设备执行 `approve` 或 `reject` 时应原样带回，电脑端据此恢复对应的 Agent 工作流。

当前 Gateway 保存最新 `request_id`，下一阶段将增加一次性授权结果队列，供真实 Agent 消费。

## Web 查询

Web 客户端先让设备进入 `thinking`，再提交：

```http
POST /api/v1/devices/{device_id}/query
Content-Type: application/json
```

```json
{"text":"系统状态","source":"web"}
```

响应在标准快照之外增加：

```json
{"input":"系统状态","tool":"orb_status"}
```

`tool` 是实际执行的本地工具名。真实 LLM/STT 尚未接入时，协议不会把回显结果伪装成模型回答。

## 设备音频

```http
POST /api/v1/devices/{device_id}/audio
Content-Type: audio/wav
```

音频必须是 16kHz、16-bit、单声道 PCM WAV，有效时长 100ms–10s，
请求体不超过 400KB。Gateway 先用配置的 STT 转写，再复用上述
`query` 流程。STT 未配置或转写失败时返回错误，并将设备转入
`error`，不会把空转写交给 Agent。
