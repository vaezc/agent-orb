#pragma once

#include <Arduino.h>
#include <ESP_I2S.h>

// DFR1221 板载 PDM 麦克风（DFRobot 官方示例引脚）。
constexpr gpio_num_t kMicClockPin = GPIO_NUM_45;
constexpr gpio_num_t kMicDataPin = GPIO_NUM_46;
constexpr gpio_num_t kAudioPowerPin = GPIO_NUM_48;
constexpr uint32_t kVoiceSampleRate = 16000;

// WakeNet 将来只接到这个边界：检测成功时 main.cpp 发送 wake，VAD 判断
// 说完时发送 speech_end。语义识别始终留在电脑端。
class OrbVoice {
 public:
  bool Begin();
  bool Ready() const { return ready_; }

 private:
  I2SClass microphone_;
  bool ready_ = false;
};
