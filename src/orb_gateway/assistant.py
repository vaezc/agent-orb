from __future__ import annotations

from dataclasses import asdict, dataclass
from datetime import datetime
from typing import Callable


@dataclass(frozen=True, slots=True)
class ToolDefinition:
    name: str
    description: str
    examples: tuple[str, ...]

    def to_dict(self) -> dict:
        value = asdict(self)
        value["examples"] = list(self.examples)
        return value


@dataclass(frozen=True, slots=True)
class AssistantResponse:
    title: str
    message: str
    tool: str


class LocalAssistant:
    """Small deterministic assistant used to validate the complete Web flow.

    It is intentionally provider-neutral. A future LLM adapter only needs to
    implement the same respond method.
    """

    TOOLS = (
        ToolDefinition("clock", "读取 Gateway 所在电脑的本地日期和时间", ("现在几点", "今天几号")),
        ToolDefinition("orb_status", "检查 Gateway 与 Orb 设备连接状态", ("系统状态", "设备状态")),
        ToolDefinition("capabilities", "列出当前 Web MVP 已接通的能力", ("你会做什么", "帮助")),
        ToolDefinition("echo", "安全回显输入，用于验证端到端传输", ("测试一下",)),
    )

    def __init__(self, device_count: Callable[[], int]):
        self._device_count = device_count

    def list_tools(self) -> list[dict]:
        return [tool.to_dict() for tool in self.TOOLS]

    def respond(self, text: str, device_id: str) -> AssistantResponse:
        cleaned = " ".join(text.strip().split())
        if not cleaned:
            raise ValueError("text must not be empty")
        if len(cleaned) > 500:
            raise ValueError("text must contain at most 500 characters")

        lowered = cleaned.casefold()
        now = datetime.now().astimezone()

        if any(word in cleaned for word in ("几点", "时间")):
            return AssistantResponse("当前时间", now.strftime("%H:%M"), "clock")
        if any(word in cleaned for word in ("几号", "日期", "星期")):
            weekdays = "一二三四五六日"
            message = f"{now:%Y年%m月%d日}，星期{weekdays[now.weekday()]}"
            return AssistantResponse("今天", message, "clock")
        if "状态" in cleaned or "health" in lowered:
            count = self._device_count()
            return AssistantResponse(
                "系统正常",
                f"Gateway 正常运行，当前登记了 {count} 个 Orb 设备。",
                "orb_status",
            )
        if any(word in cleaned for word in ("帮助", "会做什么", "功能", "tools")):
            return AssistantResponse(
                "当前能力",
                "时间日期、Gateway 状态、文字与浏览器语音输入。",
                "capabilities",
            )
        if any(word in lowered for word in ("你好", "hello", "hi")):
            return AssistantResponse("你好", "Agent Orb Web MVP 已经在线。", "echo")

        return AssistantResponse(
            "已收到",
            f"“{cleaned[:180]}”已通过完整链路。真实 LLM 尚未接入。",
            "echo",
        )
