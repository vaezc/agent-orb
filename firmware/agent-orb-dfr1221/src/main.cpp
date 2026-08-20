#include <Arduino.h>
#include <WiFi.h>

#include "gateway_client.h"
#include "orb_display.h"
#include "orb_voice.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define ORB_WIFI_SSID ""
#define ORB_WIFI_PASSWORD ""
#define ORB_GATEWAY_URL "http://192.168.1.100:8787"
#define ORB_DEVICE_ID "desk-orb"
#endif

#if __has_include("gateway_token.h")
#include "gateway_token.h"
#else
#define ORB_GATEWAY_TOKEN ""
#endif

namespace {

GatewayClient gateway(ORB_GATEWAY_URL, ORB_DEVICE_ID, ORB_GATEWAY_TOKEN);
OrbDisplay display;
OrbVoice voice;
OrbSnapshot snapshot;
uint32_t next_poll_at = 0;
uint32_t next_startup_report_at = 0;
bool startup_report_pending = true;
bool auto_stop_recording = false;
constexpr uint32_t kPollIntervalMs = 800;
constexpr uint8_t kVoiceButtonPin = 0;

void ConnectWifi() {
  if (strlen(ORB_WIFI_SSID) == 0) {
    Serial.println("[wifi] copy include/secrets.example.h to include/secrets.h first");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ORB_WIFI_SSID, ORB_WIFI_PASSWORD);
  Serial.printf("[wifi] connecting to %s", ORB_WIFI_SSID);
  const uint32_t deadline = millis() + 20000;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    display.Loop();
    delay(300);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] connected, IP=%s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[wifi] connection timed out; will retry in loop");
  }
}

bool SendAction(const char* action, const char* title = nullptr,
                const char* message = nullptr) {
  if (WiFi.status() != WL_CONNECTED) return false;
  OrbSnapshot next;
  String error;
  if (gateway.SendAction(action, snapshot, &next, &error, title, message)) {
    snapshot = next;
    display.Show(snapshot);
    return true;
  } else {
    display.ShowNetworkError(error);
    return false;
  }
}

void UploadRecording() {
  if (!voice.StopRecording()) return;
  auto_stop_recording = false;
  SendAction("speech_end");

  OrbSnapshot next;
  String error;
  Serial.printf("[voice] uploading %u bytes\n",
                static_cast<unsigned>(voice.WavSize()));
  if (gateway.SendAudio(voice.WavData(), voice.WavSize(), &next, &error)) {
    snapshot = next;
    display.Show(snapshot);
    Serial.println("[voice] transcription and query complete");
  } else {
    display.ShowNetworkError(error);
  }
}

void CancelRecording() {
  voice.StopRecording();
  auto_stop_recording = false;
  SendAction("cancel", "No speech", "Say Hi ESP again");
  Serial.println("[voice] no speech detected after wake word");
}

void HandleVoiceButton() {
  static bool stable_pressed = false;
  static bool observed_pressed = false;
  static uint32_t changed_at = 0;

  const bool pressed = digitalRead(kVoiceButtonPin) == LOW;
  if (pressed != observed_pressed) {
    observed_pressed = pressed;
    changed_at = millis();
  }
  if (pressed != stable_pressed && millis() - changed_at >= 30) {
    stable_pressed = pressed;
    if (pressed) {
      if (SendAction("wake")) {
        auto_stop_recording = false;
        voice.StartRecording();
      }
    } else if (voice.Recording()) {
      UploadRecording();
    }
  }

  if (voice.Recording()) {
    if (!voice.Capture()) {
      Serial.println("[voice] recording limit reached");
      UploadRecording();
    } else if (auto_stop_recording && voice.SpeechFinished()) {
      if (voice.HasSpeech()) {
        Serial.println("[voice] end of speech detected");
        UploadRecording();
      } else {
        CancelRecording();
      }
    }
  } else if (WiFi.status() == WL_CONNECTED && voice.DetectWakeWord()) {
    if (SendAction("wake")) {
      auto_stop_recording = true;
      voice.StartRecording();
    }
  }
}

void HandleSerialControl() {
  if (!Serial.available()) return;
  switch (Serial.read()) {
    case 'p':
      if (SendAction("wake")) {
        auto_stop_recording = true;
        voice.StartRecording();
      }
      break;
    case 'w': SendAction("wake"); break;
    case 'e': SendAction("speech_end"); break;
    case 'a': SendAction("approve"); break;
    case 'r': SendAction("reject"); break;
    case 'd': SendAction("dismiss"); break;
    case 'c': SendAction("cancel"); break;
    case 'x': SendAction("reset"); break;
    default: break;
  }
}

void ReportStartupStatus() {
  if (!startup_report_pending || millis() < next_startup_report_at) return;
  next_startup_report_at = millis() + 5000;

  OrbSnapshot next;
  String error;
  const bool wake_ready = voice.WakeWordReady();
  const char* action = wake_ready ? "reset" : "fail";
  const char* message = wake_ready ? "Say Hi ESP" : "Wake word unavailable";
  if (gateway.SendAction(action, snapshot, &next, &error, "Agent Orb", message)) {
    snapshot = next;
    display.Show(snapshot);
    startup_report_pending = false;
    Serial.printf("[startup] gateway reported: %s\n", message);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nAgent Orb DFR1221 v0.1");
  Serial.println("wake word: Hi ESP; hold BOOT for push-to-talk fallback");
  Serial.println("serial controls: p=record w=wake e=speech_end a=approve r=reject d=dismiss x=reset");
  Serial.printf("[memory] PSRAM %s, %u bytes\n",
                psramFound() ? "ready" : "unavailable",
                static_cast<unsigned>(ESP.getPsramSize()));
  display.Begin();
  voice.Begin();
  pinMode(kVoiceButtonPin, INPUT_PULLUP);
  ConnectWifi();
}

void loop() {
  display.Loop();
  HandleSerialControl();
  HandleVoiceButton();

  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t next_reconnect_at = 0;
    if (millis() >= next_reconnect_at) {
      next_reconnect_at = millis() + 10000;
      WiFi.disconnect();
      WiFi.begin(ORB_WIFI_SSID, ORB_WIFI_PASSWORD);
    }
    display.Loop();
    delay(10);
    return;
  }

  ReportStartupStatus();

  if (millis() >= next_poll_at) {
    next_poll_at = millis() + kPollIntervalMs;
    OrbSnapshot next;
    String error;
    if (gateway.FetchState(&next, &error)) {
      snapshot = next;
      display.Show(snapshot);
    } else {
      display.ShowNetworkError(error);
    }
  }
  delay(5);
}
