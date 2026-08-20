#include "orb_voice.h"

#include <ESP_SR.h>
#include <esp_heap_caps.h>

namespace {

volatile bool wake_word_detected = false;

void OnSpeechRecognitionEvent(sr_event_t event, int command_id, int phrase_id) {
  (void)command_id;
  (void)phrase_id;
  if (event == SR_EVENT_WAKEWORD || event == SR_EVENT_WAKEWORD_CHANNEL) {
    wake_word_detected = true;
  }
}

void WriteLittleEndian16(uint8_t* output, uint16_t value) {
  output[0] = value & 0xFF;
  output[1] = (value >> 8) & 0xFF;
}

void WriteLittleEndian32(uint8_t* output, uint32_t value) {
  output[0] = value & 0xFF;
  output[1] = (value >> 8) & 0xFF;
  output[2] = (value >> 16) & 0xFF;
  output[3] = (value >> 24) & 0xFF;
}

void WriteWavHeader(uint8_t* output, size_t pcm_size) {
  memcpy(output, "RIFF", 4);
  WriteLittleEndian32(output + 4, static_cast<uint32_t>(pcm_size + 36));
  memcpy(output + 8, "WAVEfmt ", 8);
  WriteLittleEndian32(output + 16, 16);
  WriteLittleEndian16(output + 20, 1);
  WriteLittleEndian16(output + 22, 1);
  WriteLittleEndian32(output + 24, kVoiceSampleRate);
  WriteLittleEndian32(output + 28, kVoiceSampleRate * sizeof(int16_t));
  WriteLittleEndian16(output + 32, sizeof(int16_t));
  WriteLittleEndian16(output + 34, 16);
  memcpy(output + 36, "data", 4);
  WriteLittleEndian32(output + 40, static_cast<uint32_t>(pcm_size));
}

}  // namespace

bool OrbVoice::Begin() {
  pinMode(kAudioPowerPin, OUTPUT);
  digitalWrite(kAudioPowerPin, HIGH);

  i2s_.setTimeout(1000);
  i2s_.setPinsPdmRx(kMicClockPin, kMicDataPin);
  ready_ = i2s_.begin(I2S_MODE_PDM_RX, kVoiceSampleRate,
                      I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
  if (ready_) {
    wav_buffer_ = static_cast<uint8_t*>(heap_caps_malloc(
        kWavHeaderSize + kPcmCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ready_ = wav_buffer_ != nullptr;
  }
  if (!ready_) i2s_.end();
  Serial.printf("[voice] PDM microphone %s (CLK=%d DATA=%d, 16 kHz mono)\n",
                ready_ ? "ready" : "failed", kMicClockPin, kMicDataPin);

  if (ready_) {
    ESP_SR.onEvent(OnSpeechRecognitionEvent);
    wake_word_ready_ = ESP_SR.begin(i2s_, nullptr, 0, SR_CHANNELS_MONO,
                                    SR_MODE_WAKEWORD, "M");
    if (wake_word_ready_) {
      Serial.println("[voice] ESP-SR AFE + WakeNet ready");
    } else {
      Serial.println("[voice] WakeNet unavailable; BOOT push-to-talk remains active");
    }
  }
  return ready_;
}

bool OrbVoice::StartRecording() {
  if (!ready_ || recording_) return false;
  if (wake_word_ready_) {
    ESP_SR.pause();
    delay(30);
  }
  pcm_size_ = 0;
  // 1024 bytes of 16 kHz mono PCM represent 32 ms of audio.  A timeout
  // shorter than that makes every direct read fail after pausing ESP-SR.
  i2s_.setTimeout(100);
  recording_started_at_ = millis();
  last_speech_at_ = recording_started_at_;
  speech_started_ = false;
  speech_finished_ = false;
  peak_mean_amplitude_ = 0.0f;
  recording_ = true;
  Serial.println("[voice] recording started");
  return true;
}

bool OrbVoice::Capture() {
  if (!recording_) return false;
  const size_t remaining = kPcmCapacity - pcm_size_;
  if (remaining == 0) return false;

  const size_t request_size = min(remaining, static_cast<size_t>(1024));
  const size_t bytes_read = i2s_.readBytes(
      reinterpret_cast<char*>(wav_buffer_ + kWavHeaderSize + pcm_size_),
      request_size);
  if (bytes_read == 0 && i2s_.lastError() != ESP_OK) {
    Serial.printf("[voice] I2S read failed: %s\n",
                  esp_err_to_name(static_cast<esp_err_t>(i2s_.lastError())));
    return false;
  }
  UpdateVoiceActivity(
      reinterpret_cast<int16_t*>(wav_buffer_ + kWavHeaderSize + pcm_size_),
      bytes_read / sizeof(int16_t));
  pcm_size_ += bytes_read;
  return pcm_size_ < kPcmCapacity;
}

bool OrbVoice::StopRecording() {
  if (!recording_) return false;
  recording_ = false;
  if (wake_word_ready_) {
    ESP_SR.resume();
  }
  if (pcm_size_ < kVoiceSampleRate * sizeof(int16_t) / 10) {
    Serial.printf("[voice] recording too short: %u bytes\n",
                  static_cast<unsigned>(pcm_size_));
    return false;
  }
  WriteWavHeader(wav_buffer_, pcm_size_);
  Serial.printf("[voice] recording ready: %u ms, %u bytes WAV, peak mean %.0f, "
                "noise floor %.0f\n",
                static_cast<unsigned>(millis() - recording_started_at_),
                static_cast<unsigned>(WavSize()), peak_mean_amplitude_,
                noise_floor_);
  return true;
}

bool OrbVoice::DetectWakeWord() {
  if (!wake_word_ready_ || recording_) return false;
  if (wake_word_detected) {
    wake_word_detected = false;
    Serial.printf("[voice] wake word detected (noise floor %.0f)\n",
                  noise_floor_);
    return true;
  }
  return false;
}

void OrbVoice::UpdateVoiceActivity(const int16_t* samples, size_t count) {
  if (count == 0) return;
  const uint32_t now = millis();
  const float amplitude = MeanAbsoluteAmplitude(samples, count);
  peak_mean_amplitude_ = max(peak_mean_amplitude_, amplitude);

  // Ignore the tail of "Hi ESP" so the wake phrase itself does not count as
  // the user's query. The DC-corrected quiet-room amplitude is about 26 on the
  // test device, while normal nearby speech is about 216.
  if (now - recording_started_at_ < 400) return;

  const float speech_threshold = max(80.0f, noise_floor_ * 3.0f);
  if (amplitude >= speech_threshold) {
    speech_started_ = true;
    last_speech_at_ = now;
  } else if (!speech_started_) {
    // Follow slowly changing fan/room noise without letting speech raise the
    // baseline that is used to detect itself.
    noise_floor_ = noise_floor_ * 0.95f + amplitude * 0.05f;
  }
  if (speech_started_ && now - last_speech_at_ >= 900) {
    speech_finished_ = true;
  } else if (!speech_started_ && now - recording_started_at_ >= 3500) {
    speech_finished_ = true;
  }
}

float OrbVoice::MeanAbsoluteAmplitude(const int16_t* samples,
                                      size_t count) const {
  if (count == 0) return 0.0f;

  // The DFR1221 PDM microphone has a noticeable DC offset. Measuring samples
  // against zero makes a quiet room look like continuous speech, so remove the
  // mean of each block before calculating its actual acoustic amplitude.
  int64_t signed_total = 0;
  for (size_t index = 0; index < count; ++index) {
    signed_total += samples[index];
  }
  const int32_t dc_offset = static_cast<int32_t>(signed_total / count);

  uint64_t deviation_total = 0;
  for (size_t index = 0; index < count; ++index) {
    const int32_t deviation = static_cast<int32_t>(samples[index]) - dc_offset;
    deviation_total += deviation < 0 ? static_cast<uint32_t>(-deviation)
                                     : static_cast<uint32_t>(deviation);
  }
  return static_cast<float>(deviation_total) / count;
}
