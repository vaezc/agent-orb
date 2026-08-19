#include "orb_display.h"

#include <Arduino.h>
#include <ESP_Panel_Library.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

namespace {

constexpr int kScreenWidth = 360;
constexpr int kScreenHeight = 360;
constexpr int kBacklightPin = 15;
constexpr int kResetPin = 47;
constexpr int kChipSelectPin = 10;
constexpr int kClockPin = 9;
constexpr int kData0Pin = 11;
constexpr int kData1Pin = 12;
constexpr int kData2Pin = 13;
constexpr int kData3Pin = 14;
constexpr uint32_t kQspiFrequencyHz = 50 * 1000 * 1000;
constexpr size_t kDrawBufferRows = 48;

ESP_PanelBus_QSPI* panel_bus = nullptr;
ESP_PanelLcd* lcd = nullptr;
lv_color_t* draw_buffer_memory = nullptr;
lv_disp_draw_buf_t draw_buffer;
lv_disp_drv_t display_driver;

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

void FlushDisplay(lv_disp_drv_t* driver, const lv_area_t* area,
                  lv_color_t* colors) {
  auto* target = static_cast<ESP_PanelLcd*>(driver->user_data);
  const bool completed = target->drawBitmapWaitUntilFinish(
      area->x1, area->y1, area->x2 - area->x1 + 1,
      area->y2 - area->y1 + 1,
      reinterpret_cast<const uint8_t*>(colors), 1000);
  if (!completed) {
    Serial.printf("[display] flush timeout for rows %d-%d\n", area->y1,
                  area->y2);
  }
  lv_disp_flush_ready(driver);
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
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x070B12), 0);
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
  lv_obj_set_style_text_font(title_label, &lv_font_simsun_16_cjk, 0);
  lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);

  message_label = MakeLabel(screen, 270, 17, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_height(message_label, 112);
  lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(message_label, &lv_font_simsun_16_cjk, 0);
  lv_obj_set_style_text_color(message_label, lv_color_hex(0xC8D0DC), 0);

  footer_label = MakeLabel(screen, 260, 116, LV_TEXT_ALIGN_CENTER);
  lv_obj_set_style_text_font(footer_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(footer_label, lv_color_hex(0x778399), 0);

  lv_label_set_text(state_label, "BOOTING");
  lv_label_set_text(title_label, "Agent Orb");
  lv_label_set_text(message_label, "Starting display and network...");
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
  Serial.println("[display] enabling ST77916 backlight control");
  pinMode(kBacklightPin, OUTPUT);
  digitalWrite(kBacklightPin, LOW);

  Serial.println("[display] starting QSPI bus");
  panel_bus = new ESP_PanelBus_QSPI(kChipSelectPin, kClockPin, kData0Pin,
                                    kData1Pin, kData2Pin, kData3Pin);
  panel_bus->configQspiFreqHz(kQspiFrequencyHz);
  if (!panel_bus->begin()) {
    Serial.println("[display] QSPI bus initialization failed");
    return false;
  }

  Serial.println("[display] creating ST77916 panel");
  lcd = new ESP_PanelLcd_ST77916(panel_bus, 16, kResetPin);
  if (!lcd->init()) {
    Serial.println("[display] panel creation failed");
    return false;
  }
  Serial.println("[display] resetting ST77916 panel");
  if (!lcd->reset()) {
    Serial.println("[display] panel reset failed");
    return false;
  }
  Serial.println("[display] sending ST77916 init sequence");
  if (!lcd->begin()) {
    Serial.println("[display] panel init sequence failed");
    return false;
  }
  Serial.println("[display] turning ST77916 panel on");
  if (!lcd->invertColor(true) || !lcd->displayOn()) {
    Serial.println("[display] panel enable failed");
    return false;
  }

  Serial.println("[display] allocating LVGL buffer");
  draw_buffer_memory = static_cast<lv_color_t*>(heap_caps_malloc(
      kScreenWidth * kDrawBufferRows * sizeof(lv_color_t),
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (draw_buffer_memory == nullptr) {
    Serial.println("[display] failed to allocate LVGL draw buffer");
    return false;
  }

  Serial.println("[display] initializing LVGL");
  lv_init();
  lv_disp_draw_buf_init(&draw_buffer, draw_buffer_memory, nullptr,
                        kScreenWidth * kDrawBufferRows);
  lv_disp_drv_init(&display_driver);
  display_driver.hor_res = kScreenWidth;
  display_driver.ver_res = kScreenHeight;
  display_driver.flush_cb = FlushDisplay;
  display_driver.draw_buf = &draw_buffer;
  display_driver.user_data = lcd;
  lv_disp_drv_register(&display_driver);

  Serial.println("[display] creating Orb UI");
  CreateUi();
  display_ready = true;
  last_tick_ms_ = millis();
  Serial.println("[display] enabling backlight");
  digitalWrite(kBacklightPin, HIGH);
  Serial.println("[display] drawing first frame");
  lv_timer_handler();
  Serial.println("[display] ST77916 + LVGL ready");
  return true;
}

void OrbDisplay::Loop() {
  if (!display_ready) return;
  const uint32_t now = millis();
  const uint32_t elapsed = now - last_tick_ms_;
  if (elapsed > 0) {
    lv_tick_inc(elapsed);
    last_tick_ms_ = now;
  }
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
