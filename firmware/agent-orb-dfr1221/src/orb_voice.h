#pragma once

#include <Arduino.h>
#include <driver/i2s.h>

// DFR1221 板载 PDM 麦克风（DFRobot 官方示例引脚）。
constexpr gpio_num_t kMicClockPin = GPIO_NUM_45;
constexpr gpio_num_t kMicDataPin = GPIO_NUM_46;
constexpr gpio_num_t kAudioPowerPin = GPIO_NUM_48;
constexpr uint32_t kVoiceSampleRate = 16000;
constexpr uint32_t kMaxRecordingSeconds = 8;

// WakeNet 只接到这个边界：检测成功时 main.cpp 发送 wake，VAD 判断
// 说完时发送 speech_end。语义识别始终留在电脑端。
class OrbVoice {
 public:
  bool Begin();
  bool StartRecording();
  bool Capture();
  bool StopRecording();
  bool DetectWakeWord();
  bool Ready() const { return ready_; }
  bool WakeWordReady() const { return wake_word_ready_; }
  bool Recording() const { return recording_; }
  bool SpeechFinished() const { return speech_finished_; }
  bool HasSpeech() const { return speech_started_; }
  uint8_t* WavData() const { return wav_buffer_; }
  size_t WavSize() const { return pcm_size_ + kWavHeaderSize; }

 private:
  static constexpr size_t kWavHeaderSize = 44;
  static constexpr size_t kPcmCapacity =
      kVoiceSampleRate * sizeof(int16_t) * kMaxRecordingSeconds;

  bool ready_ = false;
  bool wake_word_ready_ = false;
  bool recording_ = false;
  bool speech_started_ = false;
  bool speech_finished_ = false;
  uint8_t* wav_buffer_ = nullptr;
  size_t pcm_size_ = 0;
  uint32_t recording_started_at_ = 0;
  uint32_t last_speech_at_ = 0;
  float noise_floor_ = 200.0f;

  void* sr_models_ = nullptr;
  const void* wake_iface_ = nullptr;
  void* wake_model_ = nullptr;
  int16_t* wake_buffer_ = nullptr;
  size_t wake_chunk_samples_ = 0;
  size_t wake_samples_filled_ = 0;

  void UpdateVoiceActivity(const int16_t* samples, size_t count);
  float MeanAbsoluteAmplitude(const int16_t* samples, size_t count) const;
};
