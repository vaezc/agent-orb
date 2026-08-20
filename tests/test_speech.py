import io
import unittest
import wave
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

from orb_gateway.speech import WhisperCppTranscriber


def wav_bytes(frames=1_600, rate=16_000, channels=1, width=2):
    output = io.BytesIO()
    with wave.open(output, "wb") as destination:
        destination.setnchannels(channels)
        destination.setsampwidth(width)
        destination.setframerate(rate)
        destination.writeframes(b"\0" * frames * channels * width)
    return output.getvalue()


class WhisperCppTranscriberTests(unittest.TestCase):
    def test_valid_audio_runs_whisper_cli(self):
        with TemporaryDirectory() as directory:
            model = Path(directory, "model.bin")
            model.touch()
            with (
                patch("orb_gateway.speech.shutil.which", return_value="/bin/whisper-cli"),
                patch("orb_gateway.speech.subprocess.run") as run,
            ):
                run.return_value.returncode = 0
                run.return_value.stdout = "  你好，世界  \n"
                run.return_value.stderr = ""
                transcriber = WhisperCppTranscriber(str(model))
                self.assertEqual(transcriber.transcribe(wav_bytes()), "你好，世界")
            command = run.call_args.args[0]
            self.assertIn("-l", command)
            self.assertIn("zh", command)

    def test_wrong_sample_rate_is_rejected(self):
        with TemporaryDirectory() as directory:
            model = Path(directory, "model.bin")
            model.touch()
            with patch("orb_gateway.speech.shutil.which", return_value="/bin/whisper-cli"):
                transcriber = WhisperCppTranscriber(str(model))
            with self.assertRaisesRegex(ValueError, "16 kHz"):
                transcriber.transcribe(wav_bytes(rate=8_000))


if __name__ == "__main__":
    unittest.main()
