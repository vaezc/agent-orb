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

namespace {

GatewayClient gateway(ORB_GATEWAY_URL, ORB_DEVICE_ID);
OrbDisplay display;
OrbVoice voice;
OrbSnapshot snapshot;
uint32_t next_poll_at = 0;
constexpr uint32_t kPollIntervalMs = 800;

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

void SendAction(const char* action) {
  if (WiFi.status() != WL_CONNECTED) return;
  OrbSnapshot next;
  String error;
  if (gateway.SendAction(action, snapshot, &next, &error)) {
    snapshot = next;
    display.Show(snapshot);
  } else {
    display.ShowNetworkError(error);
  }
}

void HandleSerialControl() {
  if (!Serial.available()) return;
  switch (Serial.read()) {
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

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nAgent Orb DFR1221 v0.1");
  Serial.println("serial controls: w=wake e=speech_end a=approve r=reject d=dismiss x=reset");
  Serial.printf("[memory] PSRAM %s, %u bytes\n",
                psramFound() ? "ready" : "unavailable",
                static_cast<unsigned>(ESP.getPsramSize()));
  display.Begin();
  voice.Begin();
  ConnectWifi();
}

void loop() {
  HandleSerialControl();

  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t next_reconnect_at = 0;
    if (millis() >= next_reconnect_at) {
      next_reconnect_at = millis() + 10000;
      WiFi.disconnect();
      WiFi.begin(ORB_WIFI_SSID, ORB_WIFI_PASSWORD);
    }
    delay(10);
    return;
  }

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
