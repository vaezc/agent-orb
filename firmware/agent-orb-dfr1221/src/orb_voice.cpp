#include "orb_voice.h"

bool OrbVoice::Begin() {
  pinMode(kAudioPowerPin, OUTPUT);
  digitalWrite(kAudioPowerPin, HIGH);

  const i2s_config_t config = {
      .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
      .sample_rate = kVoiceSampleRate,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4,
      .dma_buf_len = 256,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0,
      .mclk_multiple = I2S_MCLK_MULTIPLE_DEFAULT,
      .bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT,
  };
  const i2s_pin_config_t pins = {
      .mck_io_num = I2S_PIN_NO_CHANGE,
      .bck_io_num = I2S_PIN_NO_CHANGE,
      .ws_io_num = kMicClockPin,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = kMicDataPin,
  };

  esp_err_t result = i2s_driver_install(I2S_NUM_0, &config, 0, nullptr);
  if (result == ESP_OK) result = i2s_set_pin(I2S_NUM_0, &pins);
  if (result == ESP_OK) result = i2s_set_pdm_rx_down_sample(I2S_NUM_0, I2S_PDM_DSR_8S);
  ready_ = result == ESP_OK;
  if (!ready_) i2s_driver_uninstall(I2S_NUM_0);
  Serial.printf("[voice] PDM microphone %s (CLK=%d DATA=%d, 16 kHz mono)\n",
                ready_ ? "ready" : "failed", kMicClockPin, kMicDataPin);
  return ready_;
}
