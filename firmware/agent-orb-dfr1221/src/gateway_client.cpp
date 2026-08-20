#include "gateway_client.h"

String GatewayClient::Endpoint(const char* resource) const {
  return base_url_ + "/api/v1/devices/" + device_id_ + "/" + resource;
}

bool GatewayClient::FetchState(OrbSnapshot* snapshot, String* error) {
  HTTPClient http;
  http.setConnectTimeout(1500);
  http.setTimeout(2500);
  if (!http.begin(Endpoint("state"))) {
    *error = "could not create HTTP request";
    return false;
  }
  const int status = http.GET();
  const bool ok = ParseResponse(http, status, snapshot, error);
  http.end();
  return ok;
}

bool GatewayClient::SendAction(const String& action,
                               const OrbSnapshot& current,
                               OrbSnapshot* next, String* error,
                               const String& title, const String& message) {
  JsonDocument body;
  body["action"] = action;
  if (!title.isEmpty()) body["title"] = title;
  if (!message.isEmpty()) body["message"] = message;
  if (!current.request_id.isEmpty()) body["request_id"] = current.request_id;
  String json;
  serializeJson(body, json);

  HTTPClient http;
  http.setConnectTimeout(1500);
  http.setTimeout(2500);
  if (!http.begin(Endpoint("actions"))) {
    *error = "could not create HTTP request";
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  const int status = http.POST(json);
  const bool ok = ParseResponse(http, status, next, error);
  http.end();
  return ok;
}

bool GatewayClient::SendAudio(uint8_t* wav_data, size_t wav_size,
                              OrbSnapshot* next, String* error) {
  HTTPClient http;
  http.setConnectTimeout(2500);
  http.setTimeout(60000);
  if (!http.begin(Endpoint("audio"))) {
    *error = "could not create audio request";
    return false;
  }
  http.addHeader("Content-Type", "audio/wav");
  const int status = http.POST(wav_data, wav_size);
  const bool ok = ParseResponse(http, status, next, error);
  http.end();
  return ok;
}

bool GatewayClient::ParseResponse(HTTPClient& http, int status,
                                  OrbSnapshot* snapshot, String* error) {
  if (status <= 0) {
    *error = HTTPClient::errorToString(status);
    return false;
  }

  const String payload = http.getString();
  JsonDocument json;
  const DeserializationError parse_error = deserializeJson(json, payload);
  if (parse_error) {
    *error = String("invalid JSON: ") + parse_error.c_str();
    return false;
  }
  if (status < 200 || status >= 300) {
    const char* detail = json["message"];
    *error = detail ? String(detail) : String("HTTP ") + status;
    return false;
  }

  snapshot->state = ParseOrbState(json["state"] | "unknown");
  snapshot->title = json["title"] | "Agent Orb";
  snapshot->message = json["message"] | "";
  snapshot->request_id = json["request_id"] | "";
  snapshot->revision = json["revision"] | 0;
  return true;
}
