#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>

#include "orb_state.h"

class GatewayClient {
 public:
  GatewayClient(const String& base_url, const String& device_id,
                const String& gateway_token)
      : base_url_(base_url), device_id_(device_id), gateway_token_(gateway_token) {}

  bool FetchState(OrbSnapshot* snapshot, String* error);
  bool SendAction(const String& action, const OrbSnapshot& current,
                  OrbSnapshot* next, String* error,
                  const String& title = "", const String& message = "");
  bool SendAudio(uint8_t* wav_data, size_t wav_size, OrbSnapshot* next,
                 String* error);

 private:
  bool ParseResponse(HTTPClient& http, int status, OrbSnapshot* snapshot,
                     String* error);
  void AddAuthorization(HTTPClient& http) const;
  String Endpoint(const char* resource) const;

  String base_url_;
  String device_id_;
  String gateway_token_;
};
