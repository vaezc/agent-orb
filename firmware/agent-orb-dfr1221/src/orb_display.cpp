#include "orb_display.h"

bool OrbDisplay::Begin() {
  Serial.println("[display] serial fallback ready");
  return true;
}

void OrbDisplay::Show(const OrbSnapshot& snapshot) {
  if (snapshot.revision == last_revision_) return;
  last_revision_ = snapshot.revision;

  Serial.println();
  Serial.println("+--------------------------------+");
  Serial.printf("| %-30s |\n", OrbStateName(snapshot.state));
  Serial.println("+--------------------------------+");
  Serial.printf("  %s\n", snapshot.title.c_str());
  Serial.printf("  %s\n", snapshot.message.c_str());
  Serial.printf("  revision: %lu\n", static_cast<unsigned long>(snapshot.revision));
  if (!snapshot.request_id.isEmpty()) {
    Serial.printf("  request: %s\n", snapshot.request_id.c_str());
  }
  if (snapshot.state == OrbState::kApproval) {
    Serial.println("  serial: [a] approve  [r] reject");
  }
}

void OrbDisplay::ShowNetworkError(const String& detail) {
  Serial.printf("[network] %s\n", detail.c_str());
}
