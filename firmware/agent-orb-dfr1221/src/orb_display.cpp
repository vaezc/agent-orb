#include "orb_display.h"

#include <Arduino.h>
#include <lvgl.h>

#include "scr_st77916.h"

namespace {

lv_obj_t* ring = nullptr;
lv_obj_t* state_label = nullptr;
lv_obj_t* title_label = nullptr;
lv_obj_t* message_label = nullptr;
lv_obj_t* footer_label = nullptr;
bool display_ready = false;

lv_color_t StateColor(OrbState state) {
  switch (state) {
    case OrbState::kListening: return lv_color_hex(0x42D3FF);
    case OrbState::kThinking: return lv_color_hex(0xA78BFA);
    case OrbState::kAnswer: return lv_color_hex(0x5EE6A8);
    case OrbState::kAttention: return lv_color_hex(0xFFD166);
    case OrbState::kApproval: return lv_color_hex(0xFF9F43);
    case OrbState::kError: return lv_color_hex(0xFF5D73);
    case OrbState::kIdle: return lv_color_hex(0x6F7C91);
    default: return lv_color_hex(0x8A96A8);
  }
}

lv_obj_t* MakeLabel(lv_obj_t* parent, int width, int y,
                    lv_text_align_t alignment) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_width(label, width);
  lv_obj_set_style_text_align(label, alignment, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0xF5F7FA), 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, y);
  return label;
}

void CreateUi() {
  lv_obj_t* screen = lv_scr_act();
  lv_obj_clean(screen);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x0057FF), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  ring = lv_obj_create(screen);
  lv_obj_set_size(ring, 338, 338);
  lv_obj_center(ring);
  lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ring, 7, 0);
  lv_obj_set_style_border_color(ring, StateColor(OrbState::kUnknown), 0);

  state_label = MakeLabel(screen, 250, -104, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_style_text_font(state_label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(state_label, StateColor(OrbState::kUnknown), 0);
  lv_obj_set_style_text_letter_space(state_label, 3, 0);

  title_label = MakeLabel(screen, 276, -57, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);

  message_label = MakeLabel(screen, 270, 17, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_height(message_label, 112);
  lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(message_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(message_label, lv_color_hex(0xFFFFFF), 0);

  footer_label = MakeLabel(screen, 260, 116, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_style_text_font(footer_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(footer_label, lv_color_hex(0xD5E1FF), 0);

  lv_label_set_text(state_label, "BOOTING");
  lv_label_set_text(title_label, "Agent Orb");
  lv_label_set_text(message_label, "Display ready");
  lv_label_set_text(footer_label, "DFR1221  |  360 x 360");
}

void UpdateUi(const OrbSnapshot& snapshot) {
  if (!display_ready) return;
  const lv_color_t color = StateColor(snapshot.state);
  lv_obj_set_style_border_color(ring, color, 0);
  lv_obj_set_style_text_color(state_label, color, 0);
  lv_label_set_text(state_label, OrbStateName(snapshot.state));
  lv_label_set_text(title_label,
                    snapshot.title.isEmpty() ? "Agent Orb" : snapshot.title.c_str());
  lv_label_set_text(message_label,
                    snapshot.message.isEmpty() ? "Ready" : snapshot.message.c_str());
  lv_label_set_text(footer_label,
                    snapshot.state == OrbState::kApproval
                        ? "Serial: A approve  |  R reject"
                        : "Connected to Snoopy");
}

}  // namespace

bool OrbDisplay::Begin() {
  Serial.println("[display] initializing code_cost ST77916 stack");
  scr_lvgl_init();
  if (disp_draw_buf == nullptr || lcd == nullptr) {
    Serial.println("[display] code_cost display initialization failed");
    return false;
  }

  CreateUi();
  display_ready = true;
  lv_obj_invalidate(lv_scr_act());
  Serial.println("[display] code_cost ST77916 + LVGL ready");
  return true;
}

void OrbDisplay::Loop() {
  if (!display_ready) return;
  lv_timer_handler();
}

void OrbDisplay::Show(const OrbSnapshot& snapshot) {
  if (snapshot.revision == last_revision_) return;
  last_revision_ = snapshot.revision;
  UpdateUi(snapshot);

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
  if (display_ready) {
    lv_obj_set_style_border_color(ring, StateColor(OrbState::kError), 0);
    lv_obj_set_style_text_color(state_label, StateColor(OrbState::kError), 0);
    lv_label_set_text(state_label, "NETWORK ERROR");
    lv_label_set_text(title_label, "Agent Orb");
    lv_label_set_text(message_label, detail.c_str());
    lv_label_set_text(footer_label, "Retrying automatically");
  }
  Serial.printf("[network] %s\n", detail.c_str());
}
