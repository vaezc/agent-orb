#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>

#include "orb_state.h"

class GatewayClient {
 public:
  GatewayClient(const String& base_url, const String& device_id)
      : base_url_(base_url), device_id_(device_id) {}

  bool FetchState(OrbSnapshot* snapshot, String* error);
  bool SendAction(const String& action, const OrbSnapshot& current,
                  OrbSnapshot* next, String* error);

 private:
  bool ParseResponse(HTTPClient& http, int status, OrbSnapshot* snapshot,
                     String* error);
  String Endpoint(const char* resource) const;

  String base_url_;
  String device_id_;
};
