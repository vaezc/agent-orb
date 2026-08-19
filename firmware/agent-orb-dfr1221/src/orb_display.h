#pragma once

#include "orb_state.h"

// DFR1221 ST77916 round display adapter. Serial output is intentionally kept as
// a diagnostic mirror for headless bring-up and field debugging.
class OrbDisplay {
 public:
  bool Begin();
  void Loop();
  void Show(const OrbSnapshot& snapshot);
  void ShowNetworkError(const String& detail);

 private:
  uint32_t last_revision_ = UINT32_MAX;
};
