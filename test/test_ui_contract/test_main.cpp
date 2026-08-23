#include <unity.h>

#include <fstream>
#include <iterator>
#include <string>

namespace {

std::string read_source(const char* path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::string method(
    const std::string& source,
    const char* begin_marker,
    const char* end_marker) {
  const auto begin = source.find(begin_marker);
  const auto end = source.find(end_marker, begin);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, begin);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, end);
  return source.substr(begin, end - begin);
}

void test_touchscreen_has_five_product_destinations_in_order() {
  const auto source = read_source("src/ui/ui_service.cpp");
  const auto rail = method(
      source,
      "void UiService::build_product_rail()",
      "void UiService::build_home_page()");
  const auto home = rail.find("LV_SYMBOL_HOME \" Home\"");
  const auto scale = rail.find("LV_SYMBOL_REFRESH \" Scale\"");
  const auto printer = rail.find("LV_SYMBOL_FILE \" Printer\"");
  const auto tags = rail.find("LV_SYMBOL_EDIT \" Tags\"");
  const auto settings = rail.find("LV_SYMBOL_SETTINGS \" Settings\"");
  TEST_ASSERT_LESS_THAN(scale, home);
  TEST_ASSERT_LESS_THAN(printer, scale);
  TEST_ASSERT_LESS_THAN(tags, printer);
  TEST_ASSERT_LESS_THAN(settings, tags);
  TEST_ASSERT_TRUE(
      rail.find("lv_obj_set_size(button, 86, 48)") != std::string::npos);
}

void test_home_weigh_opens_scale_before_refreshing() {
  const auto source = read_source("src/ui/ui_service.cpp");
  const auto callback = method(
      source,
      "void UiService::weigh_callback",
      "void UiService::tare_callback");
  TEST_ASSERT_TRUE(
      callback.find("active_page_ == ProductPage::home") !=
      std::string::npos);
  TEST_ASSERT_TRUE(
      callback.find("active_page_ = ProductPage::scale") !=
      std::string::npos);
  TEST_ASSERT_TRUE(
      callback.find("submit_weigh(millis())") != std::string::npos);
}

void test_scale_ui_uses_raw_stability_only_for_calibration_actions() {
  const auto source = read_source("src/ui/ui_service.cpp");
  const auto refresh = method(
      source,
      "void UiService::refresh_workflow()",
      "void UiService::refresh_diagnostics");
  TEST_ASSERT_TRUE(
      refresh.find(
          "scale.scale_adc_ready && scale.scale_raw_stable && !busy") !=
      std::string::npos);
  TEST_ASSERT_TRUE(
      refresh.find(
          "scale.scale_tare_ready &&\n                "
          "scale.scale_raw_stable && reference_valid") !=
      std::string::npos);
  TEST_ASSERT_TRUE(
      refresh.find("? scale.scale_stable\n            "
                   ": scale.scale_raw_stable") !=
      std::string::npos);
  TEST_ASSERT_TRUE(
      refresh.find("5. Ready to calibrate") != std::string::npos);
}

void test_scale_visual_is_a_state_gauge_with_collapsed_calibration() {
  const auto source = read_source("src/ui/ui_service.cpp");
  const auto build = method(
      source,
      "void UiService::build_scale_page()",
      "void UiService::build_printer_page()");
  TEST_ASSERT_TRUE(
      build.find("lv_arc_set_value(arc, 0)") != std::string::npos);
  TEST_ASSERT_TRUE(
      build.find("opening_positions") != std::string::npos);
  TEST_ASSERT_TRUE(
      build.find("std::pair<lv_coord_t, lv_coord_t>, 6") !=
      std::string::npos);
  TEST_ASSERT_TRUE(
      build.find(
          "lv_obj_add_flag(workflow_reference_input_, "
          "LV_OBJ_FLAG_HIDDEN)") != std::string::npos);
  TEST_ASSERT_TRUE(build.find("GROSS WEIGHT") != std::string::npos);
  TEST_ASSERT_TRUE(build.find("No captured weight") != std::string::npos);

  const auto drawer = method(
      source,
      "void UiService::set_scale_calibration_panel_open",
      "void UiService::calibrate_callback");
  TEST_ASSERT_TRUE(
      drawer.find("open ? \"Run calibration\" : \"Calibrate\"") !=
      std::string::npos);
  TEST_ASSERT_TRUE(
      drawer.find("workflow_calibration_close_button_") !=
      std::string::npos);
}

void test_repeated_native_navigation_rebuilds_one_bounded_screen() {
  const auto source = read_source("src/ui/ui_service.cpp");
  const auto build = method(
      source,
      "void UiService::build_current_screen()",
      "void UiService::build_display_self_test_screen()");
  TEST_ASSERT_TRUE(build.find("lv_obj_clean(screen)") != std::string::npos);
  TEST_ASSERT_TRUE(
      build.find("scale_calibration_panel_open_ = false") !=
      std::string::npos);
}

void test_idle_home_and_scale_refresh_do_not_copy_full_configuration() {
  const auto source = read_source("src/ui/ui_service.cpp");
  const auto refresh = method(
      source,
      "void UiService::refresh_workflow()",
      "void UiService::refresh_diagnostics");
  TEST_ASSERT_TRUE(
      refresh.find("configuration_.snapshot()") == std::string::npos);
  TEST_ASSERT_TRUE(
      refresh.find("Waiting for OpenPrintTag") == std::string::npos);
}

}  // namespace

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_touchscreen_has_five_product_destinations_in_order);
  RUN_TEST(test_home_weigh_opens_scale_before_refreshing);
  RUN_TEST(test_scale_ui_uses_raw_stability_only_for_calibration_actions);
  RUN_TEST(test_scale_visual_is_a_state_gauge_with_collapsed_calibration);
  RUN_TEST(test_repeated_native_navigation_rebuilds_one_bounded_screen);
  RUN_TEST(test_idle_home_and_scale_refresh_do_not_copy_full_configuration);
  return UNITY_END();
}
