#include "orb_voice.h"

bool OrbVoice::Begin() {
  pinMode(kAudioPowerPin, OUTPUT);
  digitalWrite(kAudioPowerPin, HIGH);

  microphone_.setPinsPdmRx(kMicClockPin, kMicDataPin);
  ready_ = microphone_.begin(
      I2S_MODE_PDM_RX,
      kVoiceSampleRate,
      I2S_DATA_BIT_WIDTH_16BIT,
      I2S_SLOT_MODE_MONO);
  Serial.printf("[voice] PDM microphone %s (CLK=%d DATA=%d, 16 kHz mono)\n",
                ready_ ? "ready" : "failed", kMicClockPin, kMicDataPin);
  return ready_;
}
