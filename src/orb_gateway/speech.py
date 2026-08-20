from __future__ import annotations

import io
import os
import shutil
import subprocess
import tempfile
import wave
from pathlib import Path
from typing import Protocol


class SpeechUnavailable(RuntimeError):
    pass


class SpeechTranscriber(Protocol):
    def transcribe(self, wav_data: bytes) -> str: ...


class DisabledSpeechTranscriber:
    def transcribe(self, wav_data: bytes) -> str:
        del wav_data
        raise SpeechUnavailable(
            "本地语音识别尚未配置；请设置 ORB_WHISPER_MODEL"
        )


class WhisperCppTranscriber:
    def __init__(
        self,
        model_path: str,
        executable: str = "whisper-cli",
        language: str = "zh",
        timeout: float = 120,
    ):
        model = Path(model_path).expanduser()
        if not model.is_file():
            raise ValueError(f"ORB_WHISPER_MODEL does not exist: {model}")
        resolved_executable = shutil.which(executable)
        if resolved_executable is None:
            raise ValueError(f"whisper.cpp executable not found: {executable}")
        self._model_path = str(model)
        self._executable = resolved_executable
        self._language = language.strip() or "auto"
        self._timeout = timeout

    def transcribe(self, wav_data: bytes) -> str:
        _validate_wav(wav_data)
        with tempfile.NamedTemporaryFile(suffix=".wav") as audio_file:
            audio_file.write(wav_data)
            audio_file.flush()
            command = [
                self._executable,
                "-m",
                self._model_path,
                "-f",
                audio_file.name,
                "-l",
                self._language,
                "-nt",
                "-np",
            ]
            try:
                result = subprocess.run(
                    command,
                    check=False,
                    capture_output=True,
                    text=True,
                    timeout=self._timeout,
                )
            except (OSError, subprocess.TimeoutExpired) as exc:
                raise SpeechUnavailable(f"本地语音识别启动失败：{exc}") from exc

        if result.returncode != 0:
            detail = " ".join(result.stderr.strip().split())[-300:]
            raise SpeechUnavailable(
                f"本地语音识别失败（退出码 {result.returncode}）：{detail}"
            )
        transcript = " ".join(result.stdout.strip().split())
        if not transcript:
            raise SpeechUnavailable("没有识别到语音")
        return transcript[:500]


def speech_from_environment() -> SpeechTranscriber:
    model_path = os.environ.get("ORB_WHISPER_MODEL", "").strip()
    if not model_path:
        return DisabledSpeechTranscriber()
    executable = os.environ.get("ORB_WHISPER_CLI", "whisper-cli").strip()
    language = os.environ.get("ORB_WHISPER_LANGUAGE", "zh").strip()
    return WhisperCppTranscriber(model_path, executable, language)


def _validate_wav(wav_data: bytes) -> None:
    try:
        with wave.open(io.BytesIO(wav_data), "rb") as source:
            channels = source.getnchannels()
            sample_width = source.getsampwidth()
            sample_rate = source.getframerate()
            frames = source.getnframes()
    except (EOFError, wave.Error) as exc:
        raise ValueError("audio body must be a valid WAV file") from exc

    if channels != 1 or sample_width != 2 or sample_rate != 16_000:
        raise ValueError("audio must be 16 kHz mono 16-bit PCM WAV")
    if frames < 1_600:
        raise ValueError("audio must contain at least 100 ms")
    if frames > 160_000:
        raise ValueError("audio must not exceed 10 seconds")
