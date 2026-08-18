#pragma once

#include <Arduino.h>

enum class OrbState {
  kIdle,
  kListening,
  kThinking,
  kAnswer,
  kAttention,
  kApproval,
  kError,
  kUnknown,
};

inline OrbState ParseOrbState(const String& value) {
  if (value == "idle") return OrbState::kIdle;
  if (value == "listening") return OrbState::kListening;
  if (value == "thinking") return OrbState::kThinking;
  if (value == "answer") return OrbState::kAnswer;
  if (value == "attention") return OrbState::kAttention;
  if (value == "approval") return OrbState::kApproval;
  if (value == "error") return OrbState::kError;
  return OrbState::kUnknown;
}

inline const char* OrbStateName(OrbState state) {
  switch (state) {
    case OrbState::kIdle: return "IDLE";
    case OrbState::kListening: return "LISTENING";
    case OrbState::kThinking: return "THINKING";
    case OrbState::kAnswer: return "ANSWER";
    case OrbState::kAttention: return "ATTENTION";
    case OrbState::kApproval: return "APPROVAL";
    case OrbState::kError: return "ERROR";
    default: return "UNKNOWN";
  }
}

struct OrbSnapshot {
  OrbState state = OrbState::kUnknown;
  String title;
  String message;
  String request_id;
  uint32_t revision = 0;
};
