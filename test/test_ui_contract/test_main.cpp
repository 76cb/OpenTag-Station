#include <unity.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>

#include "ui/weight_format.hpp"

namespace {

std::string read_source(const char* path) {
  std::ifstream input(path);
  std::string source{std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>()};
  source.erase(std::remove(source.begin(), source.end(), '\r'), source.end());
  return source;
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
  const auto home = rail.find("\"Home\"");
  const auto scale = rail.find("\"Weigh\"");
  const auto printer = rail.find("\"Assign\"");
  const auto tags = rail.find("\"Tag\"");
  const auto settings = rail.find("\"Settings\"");
  TEST_ASSERT_LESS_THAN(scale, home);
  TEST_ASSERT_LESS_THAN(printer, scale);
  TEST_ASSERT_LESS_THAN(tags, printer);
  TEST_ASSERT_LESS_THAN(settings, tags);
  TEST_ASSERT_TRUE(
      rail.find("layout::nav.y,96,48") != std::string::npos);
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
  TEST_ASSERT_TRUE(
      callback.find("!self->diagnostics_.scale_snapshot().scale_calibrated") !=
      std::string::npos);
  TEST_ASSERT_TRUE(
      callback.find("set_scale_calibration_panel_open(true)") !=
      std::string::npos);
  TEST_ASSERT_LESS_THAN(
      callback.find("submit_weigh(millis())"),
      callback.find("set_scale_calibration_panel_open(true)"));
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

void test_scale_receipt_has_explicit_update_and_collapsed_calibration() {
  const auto source = read_source("src/ui/ui_service.cpp");
  const auto build = method(
      source,
      "void UiService::build_scale_page()",
      "void UiService::build_printer_page()");
  TEST_ASSERT_TRUE(
      build.find("weight_update_callback") != std::string::npos);
  TEST_ASSERT_TRUE(build.find("opening_positions") == std::string::npos);
  TEST_ASSERT_TRUE(build.find("filament") == std::string::npos);
  TEST_ASSERT_TRUE(build.find("hub") == std::string::npos);
  TEST_ASSERT_TRUE(
      build.find("weight_policy_callback") !=
      std::string::npos);
  TEST_ASSERT_TRUE(
      build.find("Auto-update OFF") !=
      std::string::npos);
  TEST_ASSERT_TRUE(
      build.find(
          "lv_obj_add_flag(workflow_reference_input_, "
          "LV_OBJ_FLAG_HIDDEN)") != std::string::npos);
  TEST_ASSERT_TRUE(build.find("GROSS WEIGHT") != std::string::npos);
  TEST_ASSERT_TRUE(build.find("workflow_scale_capture_label_") != std::string::npos);
  TEST_ASSERT_TRUE(build.find("\"WEIGH AGAIN\", 180") != std::string::npos);
  TEST_ASSERT_TRUE(build.find("\"TARE\", 102") != std::string::npos);
  TEST_ASSERT_TRUE(build.find("\"CALIBRATE\", 160") != std::string::npos);
  TEST_ASSERT_TRUE(
      build.find("lv_obj_set_size(button, 216, 48)") != std::string::npos);
  TEST_ASSERT_TRUE(build.find("YZC-133") == std::string::npos);
  TEST_ASSERT_TRUE(build.find("5 kg") == std::string::npos);
  TEST_ASSERT_TRUE(build.find("rated capacity") == std::string::npos);

  const auto drawer = method(
      source,
      "void UiService::set_scale_calibration_panel_open",
      "void UiService::calibrate_callback");
  TEST_ASSERT_TRUE(
      drawer.find("open ? \"RUN CALIBRATION\" : \"CALIBRATE\"") !=
      std::string::npos);
  TEST_ASSERT_TRUE(
      drawer.find("workflow_calibration_close_button_") !=
      std::string::npos);
}

void test_scale_screen_has_bounded_480x320_layout_and_distinct_states() {
  const auto source = read_source("src/ui/ui_service.cpp");
  const auto build = method(
      source,
      "void UiService::build_scale_page()",
      "void UiService::build_printer_page()");
  TEST_ASSERT_TRUE(build.find("place(workflow_weight_label_,layout::gross)") != std::string::npos);
  TEST_ASSERT_TRUE(
      build.find("weight_update_=product_button(screen,layout::update") !=
      std::string::npos);
  TEST_ASSERT_TRUE(build.find("lv_obj_set_pos(button, 248, y)") != std::string::npos);
  TEST_ASSERT_TRUE(build.find("lv_obj_set_size(button, 216, 48)") != std::string::npos);
  TEST_ASSERT_TRUE(build.find("place(workflow_status_label_,layout::feedback)") !=
      std::string::npos);

  const auto refresh = method(
      source,
      "void UiService::refresh_workflow()",
      "void UiService::refresh_diagnostics");
  for (const char* state : {
           "READY", "MEASURING", "SETTLING", "STABLE", "ZERO", "ERROR",
           "SETUP REQUIRED"}) {
    TEST_ASSERT_TRUE_MESSAGE(
        refresh.find(state) != std::string::npos, state);
  }
  TEST_ASSERT_TRUE(
      refresh.find("scale.scale_calibrated ? 0x242C30 : 0x72DFBE") !=
      std::string::npos);
  TEST_ASSERT_TRUE(
      refresh.find("lv_obj_set_style_border_color(\n          "
                   "workflow_scale_indicator_") != std::string::npos);
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

void test_touchscreen_uses_signed_integer_rounded_grams() {
  using opentag::ui::rounded_grams_from_milligrams;
  TEST_ASSERT_EQUAL_INT32(0, rounded_grams_from_milligrams(499));
  TEST_ASSERT_EQUAL_INT32(1, rounded_grams_from_milligrams(500));
  TEST_ASSERT_EQUAL_INT32(1, rounded_grams_from_milligrams(1499));
  TEST_ASSERT_EQUAL_INT32(2, rounded_grams_from_milligrams(1500));
  TEST_ASSERT_EQUAL_INT32(0, rounded_grams_from_milligrams(-499));
  TEST_ASSERT_EQUAL_INT32(-1, rounded_grams_from_milligrams(-500));
  TEST_ASSERT_EQUAL_INT32(-2, rounded_grams_from_milligrams(-1500));
  TEST_ASSERT_EQUAL_INT32(0, rounded_grams_from_milligrams(0));
  TEST_ASSERT_EQUAL_INT32(5, rounded_grams_from_milligrams(5000));
  TEST_ASSERT_EQUAL_INT32(999, rounded_grams_from_milligrams(999000));
  TEST_ASSERT_EQUAL_INT32(1115, rounded_grams_from_milligrams(1115000));
  TEST_ASSERT_EQUAL_INT32(5000, rounded_grams_from_milligrams(5000000));
  TEST_ASSERT_EQUAL_INT32(-5000, rounded_grams_from_milligrams(-5000000));

  const auto source = read_source("src/ui/ui_service.cpp");
  const auto refresh = method(
      source,
      "void UiService::refresh_workflow()",
      "void UiService::refresh_diagnostics");
  TEST_ASSERT_TRUE(
      refresh.find("rounded_grams_from_milligrams") != std::string::npos);
  TEST_ASSERT_TRUE(refresh.find("%.0f") == std::string::npos);
  TEST_ASSERT_TRUE(source.find("%.0f") == std::string::npos);
  TEST_ASSERT_TRUE(
      refresh.find("inline_unit ? \"%ld g\" : \"%ld\"") !=
      std::string::npos);
}

void test_tags_page_exposes_guarded_writer_and_reader_state() {
  const auto source = read_source("src/ui/ui_service.cpp");
  const auto build = method(source, "void UiService::build_tags_page()", "void UiService::build_settings_page()");
  TEST_ASSERT_TRUE(build.find("backend_worker_.submit_writer(command)") != std::string::npos);
  TEST_ASSERT_TRUE(build.find("flow.consume(body)") != std::string::npos);
  TEST_ASSERT_TRUE(build.find("operation_id") != std::string::npos);
  TEST_ASSERT_TRUE(build.find("HTTPClient") == std::string::npos);
  TEST_ASSERT_TRUE(build.find("transport_.perform") == std::string::npos);
  TEST_ASSERT_TRUE(build.find("write_block") == std::string::npos);
  TEST_ASSERT_TRUE(build.find("use the browser") == std::string::npos);
  TEST_ASSERT_TRUE(source.find("Use browser") == std::string::npos);
  TEST_ASSERT_TRUE(source.find("Connect a phone or computer") == std::string::npos);
}

void test_clear_has_two_steps_and_large_touch_targets() {
  const auto source = read_source("src/ui/tag_flow.hpp");
  for(const char* token:{"clear_preview", "CONFIRM CLEAR", "retry_unlink", "current_checksum", "target_checksum", "generation", "previous_spool_id"})
    TEST_ASSERT_TRUE_MESSAGE(source.find(token)!=std::string::npos,token);
  const auto keyboard=read_source("src/ui/touch_input_screen.cpp");
  TEST_ASSERT_TRUE(keyboard.find("lv_scr_load(root_)")!=std::string::npos);
  TEST_ASSERT_TRUE(keyboard.find("lv_scr_load(previous_)")!=std::string::npos);
  TEST_ASSERT_TRUE(keyboard.find("backend_worker_")==std::string::npos);
  TEST_ASSERT_TRUE(keyboard.find("lv_keyboard_create")==std::string::npos);
}

void test_only_explicit_weigh_notifies_sync_and_timeout_finishes_it() {
  const auto nfc = read_source("src/application/nfc_worker.cpp");
  TEST_ASSERT_TRUE(nfc.find("submit_weigh(now, false)") != std::string::npos);
  const auto queue = read_source("src/application/scale_command_queue.cpp");
  const auto timeout = method(queue,
      "if (status.measurement_state == services::ScaleMeasurementState::timed_out",
      "operations_.succeed(");
  TEST_ASSERT_TRUE(timeout.find("weigh_finished(active_->operation_id, std::nullopt)") != std::string::npos);
  TEST_ASSERT_TRUE(queue.find("command.explicit_request && weigh_started") != std::string::npos);
  TEST_ASSERT_TRUE(queue.find("active_->explicit_request && weigh_finished") != std::string::npos);
}

void test_weigh_readback_uses_captured_policy_behind_revision_fence() {
  const auto source = read_source("src/application/weigh_commands.cpp");
  const auto capture = method(source, "void BackendWorker::begin_weigh",
                              "void BackendWorker::complete_weigh");
  for (const char* token : {"config.reconciliation.normal_tolerance_grams",
                            "config.reconciliation.warning_tolerance_grams",
                            "captured.settings_revision = revision"})
    TEST_ASSERT_TRUE_MESSAGE(capture.find(token) != std::string::npos, token);
  const auto fence = method(source, "BackendWorker::weight_fence",
                            "BackendWorker::process_weight_update");
  TEST_ASSERT_LESS_THAN(fence.find("reader_.field_on()"),
                        fence.find("captured.settings_revision != configuration_.revision()"));
  const auto readback = method(source, "BackendWorker::process_weight_update",
                               "void BackendWorker::auto_weight_update");
  TEST_ASSERT_TRUE(readback.find("captured.gross, captured.tolerances") != std::string::npos);
  TEST_ASSERT_LESS_THAN(readback.find("workflow_.apply_weight_readback"),
                        readback.find("captured.settings_revision == configuration_.revision()"));
}

}  // namespace

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_weigh_readback_uses_captured_policy_behind_revision_fence);
  RUN_TEST(test_clear_has_two_steps_and_large_touch_targets);
  RUN_TEST(test_only_explicit_weigh_notifies_sync_and_timeout_finishes_it);
  RUN_TEST(test_touchscreen_has_five_product_destinations_in_order);
  RUN_TEST(test_home_weigh_opens_scale_before_refreshing);
  RUN_TEST(test_scale_ui_uses_raw_stability_only_for_calibration_actions);
  RUN_TEST(test_scale_receipt_has_explicit_update_and_collapsed_calibration);
  RUN_TEST(test_scale_screen_has_bounded_480x320_layout_and_distinct_states);
  RUN_TEST(test_repeated_native_navigation_rebuilds_one_bounded_screen);
  RUN_TEST(test_idle_home_and_scale_refresh_do_not_copy_full_configuration);
  RUN_TEST(test_touchscreen_uses_signed_integer_rounded_grams);
  RUN_TEST(test_tags_page_exposes_guarded_writer_and_reader_state);
  return UNITY_END();
}
