from __future__ import annotations

from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from enum import Enum
from threading import Condition, RLock
from time import monotonic
from typing import Any


class OrbState(str, Enum):
    IDLE = "idle"
    LISTENING = "listening"
    THINKING = "thinking"
    ANSWER = "answer"
    ATTENTION = "attention"
    APPROVAL = "approval"
    ERROR = "error"


class InvalidTransition(ValueError):
    pass


@dataclass(slots=True)
class OrbSnapshot:
    device_id: str
    state: OrbState = OrbState.IDLE
    title: str = "Agent Orb"
    message: str = "随时可以叫我"
    revision: int = 0
    request_id: str | None = None
    updated_at: str = field(default_factory=lambda: _now())

    def to_dict(self) -> dict[str, Any]:
        value = asdict(self)
        value["state"] = self.state.value
        return value


def _now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


class OrbDevice:
    """Thread-safe state machine for one physical or simulated Orb."""

    def __init__(self, device_id: str):
        self._snapshot = OrbSnapshot(device_id=device_id)
        self._condition = Condition(RLock())

    def snapshot(self) -> OrbSnapshot:
        with self._condition:
            return OrbSnapshot(**asdict(self._snapshot))

    def wait_after(self, revision: int, timeout: float) -> OrbSnapshot:
        deadline = monotonic() + timeout
        with self._condition:
            while self._snapshot.revision <= revision:
                remaining = deadline - monotonic()
                if remaining <= 0:
                    break
                self._condition.wait(remaining)
            return OrbSnapshot(**asdict(self._snapshot))

    def apply(self, action: str, payload: dict[str, Any] | None = None) -> OrbSnapshot:
        payload = payload or {}
        with self._condition:
            state, title, message = self._transition(action, payload)
            self._snapshot.state = state
            self._snapshot.title = _clean_text(payload.get("title"), title, 64)
            self._snapshot.message = _clean_text(payload.get("message"), message, 240)
            self._snapshot.request_id = _optional_text(payload.get("request_id"), 128)
            self._snapshot.revision += 1
            self._snapshot.updated_at = _now()
            self._condition.notify_all()
            return OrbSnapshot(**asdict(self._snapshot))

    def _transition(self, action: str, payload: dict[str, Any]) -> tuple[OrbState, str, str]:
        current = self._snapshot.state
        universal: dict[str, tuple[OrbState, str, str]] = {
            "attention": (OrbState.ATTENTION, "需要关注", "有一条新消息"),
            "request_approval": (OrbState.APPROVAL, "需要你的决定", "确认继续吗？"),
            "fail": (OrbState.ERROR, "出了点问题", "请检查 Gateway 日志"),
            "reset": (OrbState.IDLE, "Agent Orb", "随时可以叫我"),
        }
        if action in universal:
            return universal[action]

        transitions: dict[OrbState, dict[str, tuple[OrbState, str, str]]] = {
            OrbState.IDLE: {
                "wake": (OrbState.LISTENING, "我在听", "请说…"),
            },
            OrbState.LISTENING: {
                "speech_end": (OrbState.THINKING, "正在思考", "正在理解你的问题"),
                "cancel": (OrbState.IDLE, "Agent Orb", "已取消"),
            },
            OrbState.THINKING: {
                "answer": (OrbState.ANSWER, "回答", "任务已经完成"),
                "cancel": (OrbState.IDLE, "Agent Orb", "已取消"),
            },
            OrbState.ANSWER: {
                "dismiss": (OrbState.IDLE, "Agent Orb", "随时可以叫我"),
                "wake": (OrbState.LISTENING, "我在听", "请说…"),
            },
            OrbState.ATTENTION: {
                "dismiss": (OrbState.IDLE, "Agent Orb", "随时可以叫我"),
                "wake": (OrbState.LISTENING, "我在听", "请说…"),
            },
            OrbState.APPROVAL: {
                "approve": (OrbState.ANSWER, "已确认", "任务将继续执行"),
                "reject": (OrbState.ANSWER, "已取消", "不会执行这项操作"),
                "cancel": (OrbState.IDLE, "Agent Orb", "已取消"),
            },
            OrbState.ERROR: {
                "dismiss": (OrbState.IDLE, "Agent Orb", "随时可以叫我"),
            },
        }

        transition = transitions.get(current, {}).get(action)
        if transition is None:
            raise InvalidTransition(f"cannot apply action '{action}' while state is '{current.value}'")
        return transition


def _clean_text(value: Any, default: str, limit: int) -> str:
    if value is None:
        return default
    if not isinstance(value, str):
        raise ValueError("title and message must be strings")
    cleaned = " ".join(value.strip().split())
    return cleaned[:limit] or default


def _optional_text(value: Any, limit: int) -> str | None:
    if value is None:
        return None
    if not isinstance(value, str):
        raise ValueError("request_id must be a string")
    cleaned = value.strip()
    return cleaned[:limit] or None


class OrbRegistry:
    def __init__(self):
        self._devices: dict[str, OrbDevice] = {}
        self._lock = RLock()

    def get(self, device_id: str) -> OrbDevice:
        if not device_id or len(device_id) > 64:
            raise ValueError("device_id must contain 1-64 characters")
        if not all(char.isalnum() or char in "-_" for char in device_id):
            raise ValueError("device_id may only contain letters, numbers, '-' and '_'")
        with self._lock:
            return self._devices.setdefault(device_id, OrbDevice(device_id))

    def list_snapshots(self) -> list[dict[str, Any]]:
        with self._lock:
            return [device.snapshot().to_dict() for device in self._devices.values()]

    def count(self) -> int:
        with self._lock:
            return len(self._devices)
