#include "ui/touch_input_screen.hpp"
#include <cstring>

namespace opentag::ui {
void TouchInputScreen::open(InputSpec spec, std::function<void(const std::string&)> accepted) {
  if(active_)return;
  input_.open(std::move(spec)); accepted_=std::move(accepted);
  previous_=lv_scr_act(); active_=true;
  if(!root_) {
    root_=lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(root_,lv_color_hex(0x101416),0);
    lv_obj_set_style_text_color(root_,lv_color_hex(0xF3F5F3),0);
    lv_obj_set_style_pad_all(root_,0,0); lv_obj_clear_flag(root_,LV_OBJ_FLAG_SCROLLABLE);
    title_=lv_label_create(root_); value_=lv_textarea_create(root_);
    lv_textarea_set_one_line(value_,true);
    lv_obj_set_style_text_font(value_,&lv_font_montserrat_20,0);
    for(auto& button:buttons_) {
      button=lv_btn_create(root_); lv_obj_set_style_pad_all(button,2,0);
      lv_obj_set_style_shadow_width(button,0,0); lv_obj_set_style_radius(button,6,0);
      lv_obj_set_style_bg_color(button,lv_color_hex(0x242C30),0);
      lv_obj_set_style_bg_color(button,lv_color_hex(0x52645F),LV_STATE_PRESSED);
      lv_obj_set_style_text_color(button,lv_color_hex(0xF3F5F3),0);
      lv_obj_add_event_cb(button,key_event,LV_EVENT_ALL,this);
      auto* label=lv_label_create(button); lv_obj_center(label);
    }
  }
  lv_scr_load(root_); draw();
}
void TouchInputScreen::draw() {
  const bool numeric=input_.spec.mode==InputMode::numeric;
  keys_=input_keys(input_.spec.mode,input_.symbols,input_.spec.decimal);
  lv_obj_set_pos(title_,8,numeric?4:8);lv_obj_set_size(title_,numeric?464:266,numeric?24:28);
  lv_obj_set_style_text_font(title_,&lv_font_montserrat_20,0);
  lv_label_set_long_mode(title_,LV_LABEL_LONG_DOT);
  const auto title=input_.spec.title+(input_.spec.unit.empty()?"":" ("+input_.spec.unit+")");
  lv_label_set_text(title_,title.c_str());
  lv_obj_set_pos(value_,8,numeric?30:52); lv_obj_set_size(value_,464,numeric?32:44);
  lv_obj_set_style_pad_ver(value_,numeric?2:8,0);
  lv_textarea_set_password_mode(value_,input_.spec.mode==InputMode::password);
  lv_textarea_set_text(value_,input_.value.c_str());lv_textarea_set_cursor_pos(value_,LV_TEXTAREA_CURSOR_LAST);
  static const char* extra[]{"[","]","{","}","<",">","?",";","'","\"","\\","|","~","`","^",",","!","@","#"};
  for(std::size_t i=0;i<buttons_.size();++i) {
    auto* button=buttons_[i];
    if(i>=keys_.count) {lv_obj_add_flag(button,LV_OBJ_FLAG_HIDDEN);continue;}
    auto& key=keys_.keys[i];
    if(input_.symbols&&input_.more&&i>=2&&i<21)key.text=extra[i-2];
    lv_obj_clear_flag(button,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(button,key.box.x,key.box.y);lv_obj_set_size(button,key.box.w,key.box.h);
    const bool action=std::string_view(key.text)=="ACTION";
    auto* label=lv_obj_get_child(button,0);
    lv_label_set_text(label,action?input_.spec.action.c_str():key.text);
    lv_obj_set_style_text_font(label,std::strlen(key.text)>1?&lv_font_montserrat_16:&lv_font_montserrat_20,0);
    lv_obj_center(label);
    lv_obj_set_style_bg_color(button,lv_color_hex(action?0x147D73:0x242C30),0);
    if(action&&!input_.valid())lv_obj_add_state(button,LV_STATE_DISABLED);else lv_obj_clear_state(button,LV_STATE_DISABLED);
  }
}
void TouchInputScreen::key_event(lv_event_t* event) {
  auto* self=static_cast<TouchInputScreen*>(lv_event_get_user_data(event));
  const auto code=lv_event_get_code(event);
  if(code!=LV_EVENT_CLICKED && code!=LV_EVENT_LONG_PRESSED_REPEAT)return;
  auto* target=static_cast<lv_obj_t*>(lv_event_get_target(event));
  for(std::size_t i=0;i<self->keys_.count;++i)if(self->buttons_[i]==target) {
    const std::string key=self->keys_.keys[i].text;
    // LVGL's bounded input repeat timer is used only for deletion. Holding a
    // submit/destructive-looking key can never submit multiple operations.
    if(code==LV_EVENT_LONG_PRESSED_REPEAT && key!="DEL")return;
    if(key=="CANCEL"){self->finish(false);return;}
    if(key=="ACTION"){if(self->input_.valid())self->finish(true);return;}
    self->input_.press(key);self->draw();return;
  }
}
void TouchInputScreen::finish(bool accepted) {
  active_=false;lv_scr_load(previous_);
  auto callback=std::move(accepted_);
  if(accepted&&callback)callback(input_.value);
}
}  // namespace opentag::ui
