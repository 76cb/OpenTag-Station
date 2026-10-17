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
  // The auto-update policy is a Settings control; the Scale page has none.
  TEST_ASSERT_TRUE(build.find("weight_policy_") == std::string::npos);
  const auto settings = method(
      source,
      "void UiService::build_settings_page()",
      "void UiService::build_workflow_screen()");
  TEST_ASSERT_TRUE(
      settings.find("weight_policy_=product_button(screen,layout::policy,"
                    "\"Auto-update weight\",weight_policy_callback,this)") !=
      std::string::npos);
  const auto settings_refresh = method(
      source,
      "void UiService::refresh_settings_page()",
      "void UiService::refresh_diagnostics");
  TEST_ASSERT_TRUE(
      settings_refresh.find(
          "lv_label_set_text(lv_obj_get_child(weight_policy_,0),"
          "automatic?\"Auto-update ON\":\"Auto-update OFF\")") !=
      std::string::npos);
  // weight_policy_ is null on the Scale page, so its refresh must not use it.
  TEST_ASSERT_TRUE(
      method(
          source,
          "void UiService::refresh_workflow()",
          "bool UiService::track_scale_operation()")
          .find("weight_policy_") == std::string::npos);
  TEST_ASSERT_TRUE(
      build.find(
          "lv_obj_add_flag(workflow_reference_input_, "
          "LV_OBJ_FLAG_HIDDEN)") != std::string::npos);
  TEST_ASSERT_TRUE(build.find("GROSS WEIGHT") != std::string::npos);
  TEST_ASSERT_TRUE(build.find("workflow_scale_capture_label_") != std::string::npos);
  TEST_ASSERT_TRUE(build.find("\"WEIGH\"") != std::string::npos);
  TEST_ASSERT_TRUE(build.find("\"TARE EMPTY PLATFORM\"") != std::string::npos);
  TEST_ASSERT_TRUE(build.find("\"UPDATE SPOOLMAN\"") != std::string::npos);
  TEST_ASSERT_TRUE(build.find("\"CALIBRATE\"") != std::string::npos);
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
  // The drawer, not the constructor, positions the two calibration actions.
  TEST_ASSERT_TRUE(
      drawer.find("lv_obj_set_pos(workflow_tare_button_,16,180)") !=
      std::string::npos);
  TEST_ASSERT_TRUE(
      drawer.find("lv_obj_set_pos(\n        workflow_calibrate_button_, 248, 128)") !=
      std::string::npos);
}

void test_scale_screen_has_bounded_480x320_layout_and_distinct_states() {
  const auto source = read_source("src/ui/ui_service.cpp");
  const auto build = method(
      source,
      "void UiService::build_scale_page()",
      "void UiService::build_printer_page()");
  TEST_ASSERT_TRUE(build.find("place(workflow_weight_label_,layout::gross)") != std::string::npos);
  TEST_ASSERT_TRUE(build.find("place(weight_update_,layout::update)") != std::string::npos);
  TEST_ASSERT_TRUE(build.find("place(workflow_weigh_button_,layout::weigh)") != std::string::npos);
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
      refresh.find("lv_obj_set_style_text_color(\n          "
                   "workflow_scale_quality_label_, "
                   "lv_color_hex(gauge_color), 0)") != std::string::npos);
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
  for(const char* token:{"clear_preview", "ERASE TAG", "retry_unlink", "current_checksum", "target_checksum", "generation", "previous_spool_id"})
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

// UI-8: a toolhead's display name already is "T1"; no literal "T" before it.
void test_printer_dialogs_do_not_prefix_the_toolhead_name() {
  const auto source = read_source("src/ui/ui_service.cpp");
  for (const char* doubled : {"printing and T\"", "verified and T\"", "Mapping T\""})
    TEST_ASSERT_TRUE_MESSAGE(source.find(doubled) == std::string::npos, doubled);
  TEST_ASSERT_TRUE(source.find("\"This printer is actively printing. Mapping \"") != std::string::npos);
}

// B2-01: the reader's bus-error counter runs since boot. The fence may only
// judge errors raised by its own inventory, and must leave the RF field off
// on every path once it has tried to turn it on.
void test_weight_fence_counts_only_its_own_bus_errors_and_drops_the_field() {
  const auto source = read_source("src/application/weigh_commands.cpp");
  const auto fence = method(source, "BackendWorker::weight_fence",
                            "BackendWorker::process_weight_update");
  TEST_ASSERT_TRUE(fence.find("bus_errors() == 0") == std::string::npos);
  const auto before = fence.find("errors_before = nfc_->reader_.bus_errors()");
  const auto on = fence.find("reader_.field_on()");
  const auto inventory = fence.find("reader_.inventory()");
  const auto after = fence.find("reader_.bus_errors() == errors_before");
  TEST_ASSERT_NOT_EQUAL(std::string::npos, before);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, after);
  TEST_ASSERT_LESS_THAN(on, before);
  TEST_ASSERT_LESS_THAN(after, inventory);
  // field_on failing still turns the field off before returning.
  const auto failed_off = fence.find("reader_.field_off()", on);
  TEST_ASSERT_LESS_THAN(inventory, failed_off);
  TEST_ASSERT_NOT_EQUAL(std::string::npos,
                        fence.find("reader_.field_off()", inventory));
}

// rc.21: the reading spinner, block bar and spool ring are built once with the
// tags page and only shown/restyled when drawn; the created-spool preview is
// sent from refresh; Home colours Spoolman-identified spools too.
void test_tag_activity_and_colour_are_built_once() {
  const auto source = read_source("src/ui/ui_service.cpp");
  const auto build = method(source, "void UiService::build_tags_page()", "void UiService::open_input(");
  for (const char* token : {"lv_spinner_create(", "lv_bar_create(", "tag_art_=lv_obj_create("})
    TEST_ASSERT_TRUE_MESSAGE(build.find(token) != std::string::npos, token);
  const auto draw = method(source, "void UiService::draw_tags()", "void UiService::refresh_tags()");
  for (const char* token : {"_create(", "lv_obj_del", "lv_obj_clean"})
    TEST_ASSERT_TRUE_MESSAGE(draw.find(token) == std::string::npos, token);
  const auto refresh = method(source, "void UiService::refresh_tags()", "void UiService::weight_update_callback(");
  TEST_ASSERT_TRUE(refresh.find("tag_command(flow.take_auto_preview())") != std::string::npos);
  TEST_ASSERT_TRUE(refresh.find("ReadState::multiple") != std::string::npos);
  const auto home = method(source, "void UiService::refresh_home_page(", "lv_obj_set_style_text_align(workflow_home_state_label_");
  TEST_ASSERT_TRUE(home.find("workflow.spool->primary_color") != std::string::npos);
  const auto flow = read_source("src/ui/tag_flow.hpp");
  TEST_ASSERT_TRUE(flow.find("TagPage::move") == std::string::npos);
  TEST_ASSERT_TRUE(flow.find("Keep the tag on the reader until it says done") == std::string::npos);
}

// rc.23: touchscreen results, settings and wording.
void test_rc23_scale_results_follow_the_spool_and_stay_visible() {
  const auto source = read_source("src/ui/ui_service.cpp");
  const auto refresh = method(source, "void UiService::refresh_workflow()", "bool UiService::track_scale_operation()");
  // P3-02 / B2-09: the Weigh page uses the spool-aware view, never the raw
  // measurement name or save state.
  TEST_ASSERT_TRUE(refresh.find("weigh_view(measured, weigh_station())") != std::string::npos);
  TEST_ASSERT_TRUE(refresh.find("measured.name.empty()") == std::string::npos);
  TEST_ASSERT_TRUE(refresh.find("!busy && !view.saving") != std::string::npos);
  const auto update = method(source, "void UiService::weight_update_callback", "void UiService::weight_policy_callback");
  TEST_ASSERT_TRUE(update.find("weigh_view(measured,self->weigh_station()).can_save") != std::string::npos);
  // UI-6: finished tare/calibration results are shown after the request ends.
  TEST_ASSERT_TRUE(refresh.find("(busy || feedback_terminal_)") != std::string::npos);
  const auto tracking = method(source, "bool UiService::track_scale_operation()", "bool UiService::home_shows_blank_tag()");
  TEST_ASSERT_TRUE(tracking.find("feedback_terminal_ = true") != std::string::npos);
  const auto build = method(source, "void UiService::build_current_screen()", "void UiService::build_display_self_test_screen()");
  TEST_ASSERT_TRUE(build.find("if (active_page_ != feedback_page_)") != std::string::npos);
  const auto printer = method(source, "void UiService::refresh_printer_page()", "void UiService::refresh_settings_page()");
  TEST_ASSERT_TRUE(printer.find("assignment_operation_id_.reset()") != std::string::npos);
  // One-liners: calibration never hides the Settings Auto-update button, and
  // Settings refresh copies no workflow snapshot.
  const auto drawer = method(source, "void UiService::set_scale_calibration_panel_open", "void UiService::calibrate_callback");
  TEST_ASSERT_TRUE(drawer.find("weight_policy_") == std::string::npos);
  const auto settings = method(source, "void UiService::refresh_settings_page()", "void UiService::refresh_diagnostics");
  TEST_ASSERT_TRUE(settings.find("workflow_.snapshot()") == std::string::npos);
  TEST_ASSERT_TRUE(settings.find("system.provisioning_requested_remaining_ms") != std::string::npos);
}

void test_rc23_setup_waits_for_its_save_and_selects_the_printer() {
  const auto source = read_source("src/ui/ui_service.cpp");
  const auto next = method(source, "void UiService::setup_next_callback", "void UiService::finish_setup_step()");
  TEST_ASSERT_TRUE(next.find("setup_advance_after_save_ = self->submit_setup_save()") != std::string::npos);
  TEST_ASSERT_TRUE(next.find("first_run_setup_.next()") == std::string::npos);
  const auto refresh = method(source, "void UiService::refresh_setup()", "void UiService::refresh_current(");
  TEST_ASSERT_TRUE(refresh.find("if (advance) {") != std::string::npos);
  TEST_ASSERT_TRUE(refresh.find("rebuild_setup_printers()") != std::string::npos);
  const auto printers = method(source, "void UiService::rebuild_setup_printers()", "void UiService::sync_setup_printer_field()");
  TEST_ASSERT_TRUE(printers.find("lv_dropdown_set_selected(") != std::string::npos);
  const auto setup = method(source, "void UiService::build_setup_screen()", "void UiService::setup_back_callback");
  TEST_ASSERT_TRUE(setup.find("lv_obj_set_size(save_button, 156, 46)") != std::string::npos);
  TEST_ASSERT_TRUE(setup.find("lv_obj_set_size(scan_button, 156, 46)") != std::string::npos);
}

void test_rc23_brightness_is_saved_and_follows_settings() {
  const auto source = read_source("src/ui/ui_service.cpp");
  const auto settings = method(source, "void UiService::build_settings_page()", "void UiService::build_workflow_screen()");
  TEST_ASSERT_TRUE(settings.find("brightness_callback,LV_EVENT_RELEASED") != std::string::npos);
  TEST_ASSERT_TRUE(settings.find("configuration_.snapshot()") == std::string::npos);
  const auto about = method(source, "void UiService::build_diagnostics_screen()", "lv_obj_t* UiService::create_setup_textarea(");
  TEST_ASSERT_TRUE(about.find("lv_slider_create") == std::string::npos);
  const auto loop = method(source, "void UiService::run_once(", "}  // namespace opentag::ui");
  TEST_ASSERT_TRUE(loop.find("apply_display_settings();") != std::string::npos);
  const auto apply = method(source, "void UiService::apply_display_settings()", "void UiService::sleep_callback");
  for (const char* token : {"dim_after_ms_ = current.device.dim_after_ms", "sleep_after_ms_ = current.device.sleep_after_ms",
                            "lv_slider_set_value(brightness_slider_"})
    TEST_ASSERT_TRUE_MESSAGE(apply.find(token) != std::string::npos, token);
}

void test_rc23_touchscreen_wording_is_plain() {
  const auto source = read_source("src/ui/ui_service.cpp") + read_source("src/ui/tag_flow.hpp");
  for (const char* jargon : {"Scanning asynchronously", "Save queued", "review persisted settings",
                             "scale queue is full", "Configuration queue is unavailable",
                             "FilaBridge readback", "Assignment unavailable", "\"ASSIGN TAG\"",
                             "\"REASSIGN\"", "\"Assign tag\"", "\"Reassign tag\"", "Ready to assign"})
    TEST_ASSERT_TRUE_MESSAGE(source.find(jargon) == std::string::npos, jargon);
  for (const char* plain : {"\"WRITE TAG\"", "\"USE FOR ANOTHER SPOOL\"", "\"Write tag\"", "\"Use for another spool\""})
    TEST_ASSERT_TRUE_MESSAGE(source.find(plain) != std::string::npos, plain);
  // The failure page shows writer errors through the plain-language map.
  TEST_ASSERT_TRUE(source.find("s.body = plain_error(message);") != std::string::npos);
  // Only ASCII on the touchscreen: the built-in fonts have no ellipsis,
  // middle dot or arrow.
  for (const char* glyph : {"\u2026", "\u00b7", "\u2192"})
    TEST_ASSERT_TRUE_MESSAGE(source.find(glyph) == std::string::npos, glyph);
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
  RUN_TEST(test_printer_dialogs_do_not_prefix_the_toolhead_name);
  RUN_TEST(test_weight_fence_counts_only_its_own_bus_errors_and_drops_the_field);
  RUN_TEST(test_tag_activity_and_colour_are_built_once);
  RUN_TEST(test_rc23_scale_results_follow_the_spool_and_stay_visible);
  RUN_TEST(test_rc23_setup_waits_for_its_save_and_selects_the_printer);
  RUN_TEST(test_rc23_brightness_is_saved_and_follows_settings);
  RUN_TEST(test_rc23_touchscreen_wording_is_plain);
  return UNITY_END();
}
