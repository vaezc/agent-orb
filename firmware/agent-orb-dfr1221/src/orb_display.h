#pragma once

#include "orb_state.h"

// 屏幕适配边界。当前实现通过串口完整呈现状态，使网络/协议可以先在
// 实机上验证。接入 DFRobot 官方 ESP32_Display_Panel + LVGL 8.4 后，
// 只需要替换这个类，不需要修改 GatewayClient 或状态机。
class OrbDisplay {
 public:
  bool Begin();
  void Show(const OrbSnapshot& snapshot);
  void ShowNetworkError(const String& detail);

 private:
  uint32_t last_revision_ = UINT32_MAX;
};
