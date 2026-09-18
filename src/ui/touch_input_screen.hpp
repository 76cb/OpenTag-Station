#pragma once
#include <functional>
#include <lvgl.h>
#include "ui/touch_input.hpp"

namespace opentag::ui {
class TouchInputScreen {
 public:
  void open(InputSpec spec, std::function<void(const std::string&)> accepted);
  bool active() const { return active_; }
 private:
  static void key_event(lv_event_t* event);
  void draw();
  void finish(bool accepted);
  TouchInput input_;
  std::function<void(const std::string&)> accepted_;
  lv_obj_t *root_{nullptr}, *previous_{nullptr}, *title_{nullptr}, *value_{nullptr};
  std::array<lv_obj_t*,36> buttons_{};
  InputKeys keys_;
  bool active_{false};
};
}  // namespace opentag::ui
