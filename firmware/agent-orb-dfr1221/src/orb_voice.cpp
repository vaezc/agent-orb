#include "orb_voice.h"

#include <esp_heap_caps.h>
#include <esp_wn_models.h>
#include <model_path.h>

namespace {

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
      .mclk_multiple = I2S_MCLK_MULTIPLE_256,
      .bits_per_chan = I2S_BITS_PER_CHAN_16BIT,
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
  if (result == ESP_OK) {
    wav_buffer_ = static_cast<uint8_t*>(heap_caps_malloc(
        kWavHeaderSize + kPcmCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (wav_buffer_ == nullptr) result = ESP_ERR_NO_MEM;
  }
  ready_ = result == ESP_OK;
  if (!ready_) i2s_driver_uninstall(I2S_NUM_0);
  Serial.printf("[voice] PDM microphone %s (CLK=%d DATA=%d, 16 kHz mono)\n",
                ready_ ? "ready" : "failed", kMicClockPin, kMicDataPin);

  if (ready_) {
    auto* models = esp_srmodel_init("model");
    sr_models_ = models;
    char* model_name =
        models == nullptr ? nullptr : esp_srmodel_filter(models, ESP_WN_PREFIX, nullptr);
    const esp_wn_iface_t* wake_iface =
        model_name == nullptr ? nullptr : esp_wn_handle_from_name(model_name);
    model_iface_data_t* wake_model =
        wake_iface == nullptr ? nullptr : wake_iface->create(model_name, DET_MODE_90);
    if (wake_model != nullptr) {
      wake_chunk_samples_ = wake_iface->get_samp_chunksize(wake_model);
      wake_buffer_ = static_cast<int16_t*>(heap_caps_malloc(
          wake_chunk_samples_ * sizeof(int16_t),
          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    wake_iface_ = wake_iface;
    wake_model_ = wake_model;
    wake_word_ready_ =
        wake_iface != nullptr && wake_model != nullptr && wake_buffer_ != nullptr;
    if (wake_word_ready_) {
      Serial.printf("[voice] WakeNet ready: %s (%u samples/frame)\n",
                    wake_iface->get_word_name(wake_model, 1),
                    static_cast<unsigned>(wake_chunk_samples_));
    } else {
      Serial.println("[voice] WakeNet unavailable; BOOT push-to-talk remains active");
    }
  }
  return ready_;
}

bool OrbVoice::StartRecording() {
  if (!ready_ || recording_) return false;
  pcm_size_ = 0;
  i2s_zero_dma_buffer(I2S_NUM_0);
  recording_started_at_ = millis();
  last_speech_at_ = recording_started_at_;
  speech_started_ = false;
  speech_finished_ = false;
  recording_ = true;
  Serial.println("[voice] recording started");
  return true;
}

bool OrbVoice::Capture() {
  if (!recording_) return false;
  const size_t remaining = kPcmCapacity - pcm_size_;
  if (remaining == 0) return false;

  const size_t request_size = min(remaining, static_cast<size_t>(1024));
  size_t bytes_read = 0;
  const esp_err_t result =
      i2s_read(I2S_NUM_0, wav_buffer_ + kWavHeaderSize + pcm_size_,
               request_size, &bytes_read, pdMS_TO_TICKS(1));
  if (result != ESP_OK) {
    Serial.printf("[voice] I2S read failed: %s\n", esp_err_to_name(result));
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
  if (pcm_size_ < kVoiceSampleRate * sizeof(int16_t) / 10) {
    Serial.printf("[voice] recording too short: %u bytes\n",
                  static_cast<unsigned>(pcm_size_));
    return false;
  }
  WriteWavHeader(wav_buffer_, pcm_size_);
  Serial.printf("[voice] recording ready: %u ms, %u bytes WAV\n",
                static_cast<unsigned>(millis() - recording_started_at_),
                static_cast<unsigned>(WavSize()));
  return true;
}

bool OrbVoice::DetectWakeWord() {
  if (!wake_word_ready_ || recording_) return false;
  auto* wake_iface = static_cast<const esp_wn_iface_t*>(wake_iface_);
  auto* wake_model = static_cast<model_iface_data_t*>(wake_model_);

  size_t bytes_read = 0;
  const size_t remaining_samples = wake_chunk_samples_ - wake_samples_filled_;
  const esp_err_t result =
      i2s_read(I2S_NUM_0, wake_buffer_ + wake_samples_filled_,
               remaining_samples * sizeof(int16_t), &bytes_read,
               pdMS_TO_TICKS(2));
  if (result != ESP_OK) return false;
  wake_samples_filled_ += bytes_read / sizeof(int16_t);
  if (wake_samples_filled_ < wake_chunk_samples_) return false;

  const float amplitude =
      MeanAbsoluteAmplitude(wake_buffer_, wake_chunk_samples_);
  if (amplitude < noise_floor_ * 4.0f) {
    noise_floor_ = noise_floor_ * 0.97f + amplitude * 0.03f;
  }
  const wakenet_state_t detected = wake_iface->detect(wake_model, wake_buffer_);
  wake_samples_filled_ = 0;
  if (detected == WAKENET_DETECTED) {
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
  const float speech_threshold = max(350.0f, noise_floor_ * 2.5f);
  if (amplitude >= speech_threshold) {
    speech_started_ = true;
    last_speech_at_ = now;
  }
  if (speech_started_ && now - last_speech_at_ >= 900) {
    speech_finished_ = true;
  } else if (!speech_started_ && now - recording_started_at_ >= 3500) {
    speech_finished_ = true;
  }
}

float OrbVoice::MeanAbsoluteAmplitude(const int16_t* samples,
                                      size_t count) const {
  uint64_t total = 0;
  for (size_t index = 0; index < count; ++index) {
    const int32_t sample = samples[index];
    total += sample < 0 ? static_cast<uint32_t>(-sample)
                        : static_cast<uint32_t>(sample);
  }
  return count == 0 ? 0.0f : static_cast<float>(total) / count;
}
