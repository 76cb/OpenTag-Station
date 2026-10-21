#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "network/backend_json.hpp"
#include "services/tag_lifecycle.hpp"
#include "ui/product_layout.hpp"

namespace opentag::ui {
enum class TagPage {
  tag,
  sources,
  catalog,
  selected,
  create,
  created,
  review,
  progress,
  ready,
  reuse,
  error,
  skip_review,
  choose
};
enum class TagAction {
  none,
  home,
  back,
  sources,
  spools,
  filaments,
  row0,
  row1,
  row2,
  previous,
  next,
  search,
  use,
  write,
  update,
  clear,
  retry,
  create,
  initial,
  remaining,
  tare,
  weigh,
  printer,
  skip,
  choose
};
// Activity drawn under the body text: an endless spinner while waiting, or a
// determinate bar when the writer reports block progress.
enum class TagIndicator : std::uint8_t { none, spinner, bar };
inline constexpr std::uint32_t neutral_spool_rgb = 0x788583;
// Spoolman color_hex ("RRGGBB" or "RRGGBBAA", optional '#'); alpha ignored.
inline std::uint32_t parse_spool_rgb(const char* text, std::uint32_t fallback = neutral_spool_rgb) {
  if (!text)
    return fallback;
  if (*text == '#')
    ++text;
  std::uint32_t rgb = 0;
  std::size_t n = 0;
  for (; text[n] && n < 9; ++n) {
    const char c = text[n];
    const int digit = c >= '0' && c <= '9'   ? c - '0'
                      : c >= 'a' && c <= 'f' ? c - 'a' + 10
                      : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                             : -1;
    if (digit < 0)
      return fallback;
    if (n < 6)
      rgb = (rgb << 4) | static_cast<std::uint32_t>(digit);
  }
  return n == 6 || n == 8 ? rgb : fallback;
}
// What Spoolman currently says about the tag on the reader when no spool is
// linked: checked (really not linked), still being looked up, or not
// reachable (the lookup failed; it is retried when Spoolman is back).
enum class SpoolLookup : std::uint8_t { checked, resolving, unavailable, failed };
// Destructive buttons (clear, skip recovery) are drawn in a distinct colour.
struct TagButton {
  layout::Box box{};
  std::string text;
  TagAction action{TagAction::none};
  bool enabled{true};
  bool destructive{false};
  // The screen's main forward action: drawn in the primary colour.
  bool primary{false};
};
struct TagScreen {
  std::string title, body;
  std::array<TagButton, 8> buttons{};
  std::size_t count{0};
  TagIndicator indicator{TagIndicator::none};
  std::uint16_t done{0}, total{0};
  // Spool colour ring beside the body (review and TAG READY pages).
  bool art{false};
  std::uint32_t art_rgb{neutral_spool_rgb};
  void button(layout::Box box, std::string text, TagAction action, bool enabled = true,
              bool destructive = false, bool primary = false) {
    buttons[count++] = {box, std::move(text), action, enabled, destructive,
                        primary && !destructive};
  }
};
// Phases in which the physical tag is being changed. The UI must never offer
// a way out during these, and must never show them for Spoolman-only work.
inline bool physical_phase(const std::string& phase) {
  return phase == "validating" || phase == "writing" || phase == "clearing" ||
         phase == "verifying" || phase == "decoding";
}
inline bool recovery_phase(const std::string& phase) {
  return phase == "unlink_pending" || phase == "association_pending" || phase == "clear_recovery" ||
         phase == "write_recovery";
}
inline std::string grams_text(JsonVariantConst value) {
  if (value.isNull())
    return "weight unknown";
  char buffer[24];
  std::snprintf(buffer, sizeof(buffer), "%.0f g", value.as<double>());
  return buffer;
}
inline layout::Box tag_body_box(const TagScreen& screen) {
  std::int16_t bottom = 266;
  for (std::size_t i = 0; i < screen.count; ++i)
    if (screen.buttons[i].box.y > 48)
      bottom = std::min(bottom, screen.buttons[i].box.y);
  // Text never runs under the activity indicator or the spool ring.
  if (screen.indicator == TagIndicator::spinner)
    bottom = std::min<std::int16_t>(bottom, layout::tag_spinner.y);
  else if (screen.indicator == TagIndicator::bar)
    bottom = std::min<std::int16_t>(bottom, layout::tag_bar.y);
  const std::int16_t width = screen.art ? layout::tag_art.x - 16 - 12 : 448;
  return {16, 48, width, static_cast<std::int16_t>(bottom - 56)};
}
inline std::string weight_text(double value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.3f", value);
  std::string text = buffer;
  while (!text.empty() && text.back() == '0')
    text.pop_back();
  if (!text.empty() && text.back() == '.')
    text.pop_back();
  return text;
}
class TagFlow {
 public:
  TagFlow() {}
  TagPage page{TagPage::tag};
  TagPage review_return{TagPage::tag};
  services::TagLifecycle lifecycle{services::TagLifecycle::no_tag};
  std::string uid, material, query, entity{"spool"}, message, phase;
  // Field Spoolman actually matched ("name", or "vendor" after a fallback);
  // echoed when paging so later pages use the same search.
  std::string search_field{"name"};
  // Plain-language description of the queued request for the wait screen.
  std::string working;
  // Why the reader rejected the tag now on it; shown for an unsupported tag.
  std::string tag_error;
  // Result of the last spool confirmation made from the Which-spool page
  // ("Linked to spool #28."), shown under the tag until Spoolman reports the
  // tag as linked or the tag changes.
  std::string notice;
  // The tag most recently on the reader; survives the reader being empty.
  std::string last_uid;
  // The unfinished write, clear or Spoolman link the writer last reported.
  // Kept until the writer reports it finished, cleared or skipped, so the
  // prompt returns after DONE, a failed retry or BACK from its review.
  struct Recovery {
    std::string phase, message;
    int spool_id{0};
  } recovery;
  // Set by the UI when a non-physical request has shown no progress for a
  // long time; offers a way back without abandoning physical work.
  bool stalled{false};
  bool land_on_last_rows{false};
  // More than one tag in the field: nothing can be read until one is removed.
  bool multiple_tags{false};
  // Only meaningful for an unlinked OpenPrintTag: "not linked" is claimed
  // only once Spoolman actually answered.
  SpoolLookup lookup{SpoolLookup::checked};
  std::string lookup_error;  // plain reason when lookup == failed
  // Set when this station's own create_spool returned the new spool; the UI
  // sends its tag preview once (take_auto_preview), with no extra stop.
  bool auto_preview{false};
  // Spools that may belong to this tag (vendor/material or product match).
  // The user picks one; the UI submits it as an explicit confirmation.
  std::vector<std::pair<int, std::string>> candidates;
  int confirm_spool_id{0};
  // Shown on the create page when a required value is missing.
  std::string create_hint;
  int current_spool{0}, from_spool{0};
  unsigned offset{0}, row{0};
  // Start offsets of the pages before this one. A page can hold fewer than 8
  // rows (the station shrinks oversized pages), so PREV returns to the exact
  // offset it came from instead of subtracting a page size.
  std::array<unsigned, 32> previous_offsets{};
  std::size_t previous_count{0};
  double initial{1000}, remaining{1000}, tare{0};
  // False until the empty-spool weight comes from Spoolman or the user. A
  // silent 0 g default overstated remaining filament on every later weigh.
  bool tare_known{false};
  bool waiting{false}, rewrite{false}, reading_request{false};
  std::uint64_t operation{0};
  // When the current touch request was queued, for the "taking longer than
  // usual" escape on Spoolman-only work.
  std::uint32_t wait_started_ms{0};
  // Phase the stall timer is running for; a new phase restarts it.
  std::string stall_phase;
  network::BackendJsonAllocator allocator;
  JsonDocument view{&allocator}, selected{&allocator}, catalog_page{&allocator};

  std::string request(const char* action) {
    JsonDocument command(&allocator);
    command["action"] = action;
    return encode(command);
  }
  std::string browse() {
    JsonDocument command(&allocator);
    command["action"] = "catalog";
    command["entity"] = entity;
    command["search"] = query;
    command["offset"] = offset;
    if (!query.empty() && offset > 0 && search_field == "vendor")
      command["search_field"] = "vendor";
    return encode(command);
  }
  void fail(std::string reason) {
    waiting = false;
    land_on_last_rows = false;
    message = std::move(reason);
    page = TagPage::error;
  }
  // Wait, leave and tag-observation transitions. UiService supplies the
  // receipts, reader state and clock; the rules live here so they are
  // host-tested.
  std::string search(std::string text) {
    query = std::move(text);
    offset = row = 0;
    previous_count = 0;
    land_on_last_rows = false;
    return browse();
  }
  void begin_wait(std::uint64_t operation_id, std::uint32_t now_ms) {
    waiting = true;
    operation = operation_id;
    page = TagPage::progress;
    phase = stall_phase = "queued";
    message = working;
    stalled = false;
    wait_started_ms = now_ms;
  }
  // BACK TO HOME from a stalled wait: stop following the request.
  void abandon_wait() {
    waiting = false;
    operation = 0;
    stalled = false;
  }
  // Leaving the tag workspace for the home, weigh or printer page.
  void leave_page() {
    page = TagPage::tag;
    phase.clear();
    stalled = false;
    resume_recovery();
  }
  // The reader's current tag: a different tag restarts the flow, whether it
  // replaced the previous one between two reads or after an empty reader.
  void observe_tag(const std::string& incoming_uid, const std::string* error_message) {
    // Never while a tag is being changed or linked, including work another
    // client started: that progress page must stay until it ends.
    const bool changing = physical_phase(phase) || phase == "associating" || phase == "unlinking";
    if (!waiting && !changing && !incoming_uid.empty() && !last_uid.empty() &&
        incoming_uid != last_uid && !recovery_phase(phase)) {
      page = TagPage::tag;
      phase.clear();
      selected.clear();
      from_spool = 0;
      resume_recovery();
    }
    if (!incoming_uid.empty() && incoming_uid != last_uid)
      notice.clear();
    uid = incoming_uid;
    if (!incoming_uid.empty())
      last_uid = incoming_uid;
    // Kept apart from message: that may hold recovery instructions.
    if (error_message)
      tag_error = *error_message;
    else
      tag_error.clear();
  }
  // The reader's lifecycle for the tag on it. A tag Spoolman now links no
  // longer needs the confirmation notice: the page says "Linked to spool".
  void observe_lifecycle(services::TagLifecycle current) {
    lifecycle = current;
    if (current != services::TagLifecycle::unlinked)
      notice.clear();
  }
  // The Which-spool choice was handed to the station (or refused because it
  // is busy). Shown on the tag page; the lookup result replaces it.
  void confirmation_submitted(int spool, bool accepted) {
    notice = accepted ? "Linked to spool #" + std::to_string(spool) + ". Checking it in Spoolman..."
                      : "The station is busy. Choose the spool again in a moment.";
  }
  // The queued request failed without publishing a writer snapshot.
  void operation_failed(std::string reason) {
    operation = 0;
    fail(std::move(reason));
  }
  // A Spoolman-only request (never a physical write) that shows no result
  // for 45 s offers a way back instead of an endless wait screen. Each step
  // gets its own 45 s: linking after a long physical write is not "late".
  void update_stall(std::uint32_t now_ms) {
    if (phase != stall_phase) {
      stall_phase = phase;
      wait_started_ms = now_ms;
    }
    stalled = waiting && !physical_phase(phase) &&
              static_cast<std::uint32_t>(now_ms - wait_started_ms) >= 45000U;
  }
  bool consume(const network::ResponseBody& body) {
    JsonDocument incoming(&allocator);
    if (body.size() > 24576 || deserializeJson(incoming, body.data(), body.size(),
                                               DeserializationOption::NestingLimit(12))) {
      fail("Station response unavailable; try again");
      return false;
    }
    if (operation && incoming["operation_id"].as<std::uint64_t>() != operation)
      return false;
    const std::string result_uid = incoming["uid"] | "";
    const std::string result_phase = incoming["phase"] | "";
    // The recovery record is station-wide: once the writer reports it ended,
    // forget it even when that result is for a tag no longer on the reader.
    if (result_phase == "complete" || result_phase == "cleared" ||
        result_phase == "recovery_discarded")
      recovery = {};
    if (!operation && !result_uid.empty() && result_uid != uid &&
        (result_phase == "complete" || result_phase == "cleared" || result_phase == "preview" ||
         result_phase == "clear_preview"))
      return false;
    // A Spoolman edit made in the browser is not part of the touch flow.
    if (result_phase == "editing" || result_phase == "updated")
      return true;
    view.set(incoming);
    phase = view["phase"] | "";
    message = view["message"] | "";
    if (recovery_phase(phase))
      recovery = {phase, message, view["spool_id"] | 0};
    if (phase == "catalog") {
      if (!view["items"].is<JsonArray>() || view["items"].size() > 8) {
        fail("Inventory page exceeds limit");
        return false;
      }
      catalog_page.set(view);
      search_field = view["search_field"] | "name";
      const auto size = view["items"].size();
      row = land_on_last_rows && size > 0 ? static_cast<unsigned>(((size - 1) / 3) * 3) : 0;
      land_on_last_rows = false;
      page = TagPage::catalog;
    } else if (phase == "preview" || phase == "clear_preview")
      page = TagPage::review;
    else if (phase == "spool_selected") {
      selected.set(view["spool"]);
      page = TagPage::created;
      // Only the answer to this screen's own request; a spool created in the
      // browser must never start a touchscreen tag preview.
      auto_preview = operation != 0;
    } else if (phase == "complete")
      page = TagPage::ready;
    else if (phase == "cleared") {
      page = TagPage::reuse;
      current_spool = from_spool = 0;
      rewrite = false;
    } else if (recovery_phase(phase))
      page = TagPage::tag;
    else if (phase == "failed" || phase == "recovery_discarded")
      page = TagPage::error;
    else if (phase != "idle") {
      page = TagPage::progress;
      return true;
    }
    waiting = false;
    operation = 0;
    return true;
  }
  std::string act(TagAction action) {
    if (waiting)
      return {};
    JsonDocument command(&allocator);
    switch (action) {
      case TagAction::sources:
        from_spool = current_spool;
        rewrite = lifecycle == services::TagLifecycle::linked ||
                  lifecycle == services::TagLifecycle::unlinked;
        page = TagPage::sources;
        return {};
      case TagAction::spools:
      case TagAction::filaments:
        entity = action == TagAction::spools ? "spool" : "filament";
        offset = row = 0;
        previous_count = 0;
        land_on_last_rows = false;
        query.clear();
        search_field = "name";
        page = TagPage::catalog;
        return browse();
      case TagAction::previous:
        if (row >= 3) {
          row -= 3;
          return {};
        }
        if (previous_count > 0) {
          offset = previous_offsets[--previous_count];
          land_on_last_rows = true;
          return browse();
        }
        return {};
      case TagAction::next:
        if (row + 3 < catalog_page["items"].size()) {
          row += 3;
          return {};
        }
        if (catalog_page["has_more"] | false) {
          const unsigned next = catalog_page["next_offset"] | (offset + 8U);
          if (next <= offset) return {};
          if (previous_count < previous_offsets.size())
            previous_offsets[previous_count++] = offset;
          offset = next;
          return browse();
        }
        return {};
      case TagAction::choose:
        if (candidates.empty())
          return {};
        page = TagPage::choose;
        return {};
      case TagAction::row0:
      case TagAction::row1:
      case TagAction::row2: {
        if (page == TagPage::choose) {
          const auto pick =
              static_cast<std::size_t>(action) - static_cast<std::size_t>(TagAction::row0);
          if (pick < candidates.size())
            confirm_spool_id = candidates[pick].first;
          page = TagPage::tag;
          return {};
        }
        const auto index =
            row + static_cast<unsigned>(action) - static_cast<unsigned>(TagAction::row0);
        if (index >= catalog_page["items"].size())
          return {};
        selected.set(catalog_page["items"][index]);
        // A spool goes straight to the tag review: it shows the spool, any
        // move from another spool, and the single WRITE TAG confirmation.
        if (entity == "spool")
          return preview_selected(TagPage::catalog);
        page = TagPage::selected;
        return {};
      }
      case TagAction::use:
        if (page == TagPage::selected && entity == "filament") {
          weights();
          create_hint.clear();
          page = TagPage::create;
          return {};
        }
        return preview_selected(page);
      case TagAction::update:
        if (current_spool <= 0)
          return {};
        review_return = TagPage::tag;
        command["action"] = "preview";
        command["mode"] = "rewrite";
        command["spool_id"] = current_spool;
        break;
      case TagAction::write:
        if (page == TagPage::skip_review) {
          command["action"] = "discard_recovery";
          // The tag the recovery is for: the writer's own record, else the
          // tag on the reader.
          const char* recovery_uid = view["uid"].as<const char*>();
          command["uid"] = recovery_uid && *recovery_uid ? std::string(recovery_uid)
                           : !uid.empty()               ? uid
                                                        : last_uid;
          break;
        }
        if (page != TagPage::review || (view["semantic_no_change"] | false))
          return {};
        command["action"] = phase == "clear_preview" ? "clear" : "write";
        for (auto key : {"uid", "generation", "spool_id", "previous_spool_id", "current_checksum",
                         "target_checksum"})
          if (!view[key].isNull())
            command[key] = view[key];
        break;
      case TagAction::clear:
        review_return = TagPage::tag;
        command["action"] = "clear_preview";
        break;
      case TagAction::skip:
        if (!recovery_phase(phase))
          return {};
        review_return = TagPage::tag;
        page = TagPage::skip_review;
        return {};
      case TagAction::retry:
        if (phase == "write_recovery") {
          review_return = TagPage::tag;
          command["action"] = "preview";
          command["mode"] = "rewrite";
          // The view may since hold the result of a failed retry.
          if (recovery_phase(view["phase"] | ""))
            command["spool_id"] = view["spool_id"];
          else
            command["spool_id"] = recovery.spool_id;
          break;
        }
        command["action"] = phase == "association_pending" ? "retry_association"
                            : phase == "clear_recovery"    ? "clear_preview"
                                                           : "retry_unlink";
        break;
      case TagAction::create:
        if (!tare_known) {
          create_hint =
              "Enter the empty spool weight first (weigh an empty reel of the same type, or use the maker's value).";
          return {};
        }
        // Stay on the form: the entered values are kept for correction.
        if (initial <= 0 || remaining < 0 || remaining > initial || tare < 0 || initial > 100000 ||
            tare > 100000) {
          create_hint =
              "Check the spool weights: remaining cannot be more than initial, and none can be above 100000 g.";
          return {};
        }
        create_hint.clear();
        command["action"] = "create_spool";
        command["spool"]["filament_id"] = selected["id"];
        command["spool"]["initial_weight"] = initial;
        command["spool"]["remaining_weight"] = remaining;
        command["spool"]["spool_weight"] = tare;
        break;
      case TagAction::back:
        if (page == TagPage::selected || page == TagPage::create)
          page = TagPage::catalog;
        else if (page == TagPage::catalog)
          page = TagPage::sources;
        else if (page == TagPage::review || page == TagPage::skip_review)
          page = review_return;
        else if (page == TagPage::choose)
          page = TagPage::tag;
        else
          page = TagPage::tag;
        if (page == TagPage::tag)
          resume_recovery();
        return {};
      default:
        return {};
    }
    return encode(command);
  }
  // The tag preview for a spool this screen just created, exactly once.
  std::string take_auto_preview() {
    const bool due = auto_preview && page == TagPage::created && !waiting;
    auto_preview = false;
    return due ? act(TagAction::use) : std::string();
  }
  TagScreen screen() const;

 private:
  std::string preview_selected(TagPage back_to) {
    JsonDocument command(&allocator);
    review_return = back_to;
    command["action"] = "preview";
    command["spool_id"] = selected["id"];
    command["mode"] = rewrite ? "rewrite" : "blank";
    return encode(command);
  }
  void resume_recovery() {
    if (!recovery.phase.empty()) {
      phase = recovery.phase;
      message = recovery.message;
    }
  }
  void weights() {
    initial = selected["weight"] | 1000.;
    if (initial <= 0)
      initial = 1000;
    remaining = initial;
    // Filament empty-reel weight, else the vendor's; otherwise ask the user.
    JsonVariantConst package = selected["spool_weight"],
                     vendor = selected["vendor"]["empty_spool_weight"];
    tare_known = package.is<double>() || vendor.is<double>();
    tare = package.is<double>()  ? package.as<double>()
           : vendor.is<double>() ? vendor.as<double>()
                                 : 0.;
  }
  static const char* describe(std::string_view action) {
    if (action == "catalog")
      return "Searching Spoolman...";
    if (action == "preview" || action == "clear_preview")
      return "Reading the tag...";
    if (action == "write")
      return "Writing the tag...";
    if (action == "clear")
      return "Clearing the tag...";
    if (action == "create_spool")
      return "Creating the spool in Spoolman...";
    if (action == "retry_association")
      return "Saving the link in Spoolman...";
    if (action == "retry_unlink")
      return "Removing the link in Spoolman...";
    if (action == "discard_recovery")
      return "Stopping recovery...";
    return "Working on your request...";
  }
  std::string encode(JsonDocument& command) {
    if (command.overflowed() || measureJson(command) > 4096) {
      fail("Command exceeds station limits");
      return {};
    }
    working = describe(std::string_view(command["action"] | ""));
    stalled = false;
    reading_request = std::string_view(command["action"] | "") == "preview" ||
                      std::string_view(command["action"] | "") == "clear_preview";
    std::string result;
    serializeJson(command, result);
    return result;
  }
};
inline std::string filament_name(JsonVariantConst item) {
  auto f = item["filament"].is<JsonObjectConst>() ? item["filament"] : item;
  std::string vendor = f["manufacturer"] | (f["vendor"]["name"] | "");
  return vendor + (vendor.empty() ? "" : " ") + std::string(f["name"] | "Unnamed filament");
}
// The spool's colour from a writer view: Spoolman's filament color_hex, else
// the proposed OpenPrintTag primary colour, else neutral grey.
inline std::uint32_t view_spool_rgb(JsonVariantConst view) {
  const char* hex = view["spool"]["filament"]["color_hex"].as<const char*>();
  if (hex && *hex)
    return parse_spool_rgb(hex);
  JsonVariantConst color = view["proposed"]["primary_color"];
  if (color.is<JsonArrayConst>() && color.size() >= 3)
    return (std::uint32_t(color[0].as<unsigned>() & 0xFFU) << 16) |
           (std::uint32_t(color[1].as<unsigned>() & 0xFFU) << 8) |
           std::uint32_t(color[2].as<unsigned>() & 0xFFU);
  return neutral_spool_rgb;
}
// Writer and station errors in plain words. Known internal messages are
// replaced by what the user can do; anything unknown is shown as it is.
inline std::string plain_error(const std::string& raw) {
  const auto has = [&](const char* part) { return raw.find(part) != std::string::npos; };
  if (has("unlink refused") || has("acquired an owner during unlink") ||
      has("owner changed after confirmation"))
    return "Spoolman's record for this tag changed while the station was working on it. Check the spool in Spoolman, then try again.";
  if (has("Multiple NFC UID owners") || has("NFC UID ownership") || has("instance UUID conflict") ||
      has("Conflicting instance UUID") || has("belongs to another Spoolman spool") ||
      has("not uniquely the target spool"))
    return "More than one Spoolman spool claims this tag. Remove it from the extra spools in Spoolman, then try again.";
  if (has("identity and NFC UID extra fields") || has("distinct identity fields"))
    return "Spoolman is missing the extra fields this station needs. Set them up in the browser (Settings), then try again.";
  if (has("workspace unavailable") || has("allocation failed"))
    return "The station ran short of working memory for this step. Try again; restart the station if it keeps happening.";
  if (has("queue is full") || has("queue unavailable") || has("queue is unavailable"))
    return "The station is busy. Try again in a moment.";
  if (has("HTTP response exceeds"))
    return "Spoolman sent more data than the station can handle. Try a narrower search.";
  if (has("does not match this exact preview") || has("preview again") || has("stale") ||
      has("refresh selection") || has("refresh before"))
    return "Something changed since the review. Go back and review it again.";
  if (has("readback mismatch") || has("ID mismatch"))
    return "Spoolman returned different data than expected. Check the spool in Spoolman, then try again.";
  if (has("Malformed"))
    return "Spoolman returned data the station could not read. Check the record in Spoolman.";
  return raw;
}
// "Not written: material, name" for the data a tag write leaves out
// (writer preview dropped_fields). Empty when nothing is dropped.
inline std::string dropped_fields_text(JsonVariantConst dropped) {
  if (!dropped.is<JsonArrayConst>() || dropped.size() == 0)
    return {};
  static constexpr std::array<std::pair<const char*, const char*>, 6> names{{
      {"name", "name"}, {"vendor", "maker"}, {"material", "material"},
      {"material_type", "material type"}, {"article_number", "article number"},
      {"color", "color"}}};
  std::string text = "Not written: ";
  std::size_t shown = 0, total = 0;
  std::array<const char*, 6> seen{};
  for (JsonVariantConst item : dropped.as<JsonArrayConst>()) {
    const char* field = item.is<JsonObjectConst>() ? item["field"].as<const char*>()
                                                    : item.as<const char*>();
    if (!field)
      continue;
    const char* plain = nullptr;
    for (const auto& entry : names)
      if (std::string_view(field) == entry.first)
        plain = entry.second;
    if (!plain)
      plain = "other data";
    if (std::find(seen.begin(), seen.begin() + total, plain) != seen.begin() + total)
      continue;
    if (total < seen.size())
      seen[total] = plain;
    ++total;
    if (shown < 3) {
      text += (shown ? ", " : "") + std::string(plain);
      ++shown;
    }
  }
  if (total == 0)
    return {};
  if (total > shown)
    text += " and " + std::to_string(total - shown) + " more";
  return text;
}
// The writer's other preview warnings, as the browser's "Metadata notices":
// everything except what the review already says (a move, the non-atomic
// write, a full rewrite) and the dropped-field sentences shown separately.
// Missing optional fields are summarised; anything else is quoted.
inline std::string preview_notices_text(JsonVariantConst view) {
  JsonVariantConst warnings = view["warnings"];
  if (!warnings.is<JsonArrayConst>())
    return {};
  const auto dropped_value = [&](std::string_view text) {
    JsonVariantConst dropped = view["dropped_fields"];
    if (!dropped.is<JsonArrayConst>())
      return false;
    for (JsonVariantConst item : dropped.as<JsonArrayConst>()) {
      const char* value = item["value"].as<const char*>();
      if (value && *value && text.find(value) != std::string_view::npos)
        return true;
    }
    return false;
  };
  std::string optional, other;
  std::size_t optional_count = 0, other_count = 0;
  for (JsonVariantConst warning : warnings.as<JsonArrayConst>()) {
    const char* raw = warning.as<const char*>();
    if (!raw || !*raw)
      continue;
    const std::string_view text(raw);
    if (text.find("association will move") != std::string_view::npos ||
        text.find("not atomic") != std::string_view::npos ||
        text.find("Full rewrite") != std::string_view::npos || dropped_value(text))
      continue;
    static constexpr std::string_view prefix = "missing recommended ";
    if (text.substr(0, prefix.size()) == prefix) {
      if (optional_count < 3) {
        std::string name(text.substr(prefix.size()));
        std::replace(name.begin(), name.end(), '_', ' ');
        optional += (optional_count ? ", " : "") + name;
      }
      ++optional_count;
    } else {
      if (other_count == 0)
        other = std::string(text);
      ++other_count;
    }
  }
  std::string result;
  if (optional_count) {
    result = "Optional data not set: " + optional;
    if (optional_count > 3)
      result += " and " + std::to_string(optional_count - 3) + " more";
  }
  if (other_count) {
    if (!result.empty())
      result += "\n";
    result += other;
    if (other_count > 1)
      result += " (and " + std::to_string(other_count - 1) + " more)";
  }
  return result;
}
inline std::string diameter_text(JsonVariantConst value) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%.2f mm", value | 1.75);
  return buffer;
}
inline TagScreen TagFlow::screen() const {
  TagScreen s;
  s.title = "Manage tag";
  const auto action = [&](int n, const char* label, TagAction a) {
    // The first of a list of actions is the recommended one.
    s.button({16, static_cast<std::int16_t>(112 + n * 50), 448, 44}, label, a,
             true, false, n == 0);
  };
  const auto back = [&] {
    s.button({8, 266, 140, 46}, "BACK", TagAction::back);
  };
  const auto primary = [&](const char* text, TagAction a, bool enabled = true,
                           bool destructive = false) {
    s.button({156, 266, 316, 46}, text, a, enabled, destructive, true);
  };
  switch (page) {
    case TagPage::tag:
      if (recovery_phase(phase)) {
        s.title = phase == "write_recovery"        ? "Finish writing this tag"
                  : phase == "clear_recovery"      ? "Finish clearing this tag"
                  : phase == "association_pending" ? "Link not saved yet"
                                                   : "Unlink not finished";
        s.body = message;
        action(1,
               phase == "write_recovery"        ? "FINISH WRITING"
               : phase == "clear_recovery"      ? "FINISH CLEARING"
               : phase == "association_pending" ? "TRY SAVING LINK AGAIN"
                                                : "TRY UNLINKING AGAIN",
               TagAction::retry);
        s.button({16, 212, 448, 44}, "SKIP RECOVERY", TagAction::skip, true, true);
      } else if (multiple_tags) {
        s.body = "Two tags on the reader. Remove one.";
      } else if (lifecycle == services::TagLifecycle::blank_compatible) {
        s.body = "BLANK TAG\nReady to use\n" + uid;
        action(0, "WRITE TAG", TagAction::sources);
      } else if (lifecycle == services::TagLifecycle::linked) {
        s.body = material + "\nLinked to spool #" + std::to_string(current_spool);
        action(0, "UPDATE TAG", TagAction::update);
        action(1, "USE FOR ANOTHER SPOOL", TagAction::sources);
        s.button({16, 212, 448, 44}, "CLEAR / REUSE", TagAction::clear, true, true);
      } else if (lifecycle == services::TagLifecycle::unlinked && !candidates.empty()) {
        s.body = material + "\nThis may be one of your Spoolman spools.";
        action(0, "CHOOSE WHICH SPOOL", TagAction::choose);
        action(1, "LINK TO A SPOOL", TagAction::sources);
        s.button({16, 212, 448, 44}, "CLEAR / REUSE", TagAction::clear, true, true);
      } else if (lifecycle == services::TagLifecycle::unlinked &&
                 lookup == SpoolLookup::failed) {
        // Spoolman answered, but the lookup failed: say why and still allow
        // linking by hand.
        s.body = material + "\nThe Spoolman lookup failed" +
            (lookup_error.empty() ? std::string(".") : ": " + plain_error(lookup_error));
        action(0, "LINK TO A SPOOL", TagAction::sources);
        s.button({16, 212, 448, 44}, "CLEAR / REUSE", TagAction::clear, true, true);
      } else if (lifecycle == services::TagLifecycle::unlinked &&
                 lookup != SpoolLookup::checked) {
        // Not "not linked": Spoolman has not answered. Linking needs
        // Spoolman, so it is not offered until it does.
        s.body = material + (lookup == SpoolLookup::resolving
                                 ? "\nLooking up this spool in Spoolman..."
                                 : "\nSpoolman is not reachable. The tag's link cannot be checked right now.\nIt will be looked up again when Spoolman is back.");
        s.button({16, 212, 448, 44}, "CLEAR / REUSE", TagAction::clear, true, true);
      } else if (lifecycle == services::TagLifecycle::unlinked) {
        s.body = material + "\nNot linked to a Spoolman spool yet";
        action(0, "LINK TO A SPOOL", TagAction::sources);
        s.button({16, 162, 448, 44}, "CLEAR / REUSE", TagAction::clear, true, true);
      } else if (lifecycle == services::TagLifecycle::no_tag)
        s.body = "PLACE A TAG\nSet an NFC tag on the reader.";
      else if (lifecycle == services::TagLifecycle::unsupported)
        s.body = "This tag can't be used as it is\n" + tag_error +
                 "\nUse a blank NXP ICODE SLIX2 tag.";
      else if (lifecycle == services::TagLifecycle::unsupported_type)
        // #93: found and named, nothing read. tag_error holds the kind.
        s.body = "Tag type not supported yet\n" + tag_error + "\nID " + uid +
                 "\nUse an OpenPrintTag on NXP ICODE SLIX2.";
      else {
        s.title = "Reading tag";
        s.body = "Hold the spool still.";
        s.indicator = TagIndicator::spinner;
      }
      if (!notice.empty() && lifecycle == services::TagLifecycle::unlinked &&
          !recovery_phase(phase) && !multiple_tags)
        s.body += "\n" + notice;
      s.button({8, 266, 464, 46}, "DONE", TagAction::home);
      break;
    case TagPage::sources:
      s.title = from_spool ? "Use for another spool" : "Write tag";
      s.body = "How do you want to choose the filament?";
      action(0, "MY SPOOLS", TagAction::spools);
      action(1, "MY FILAMENTS", TagAction::filaments);
      back();
      break;
    case TagPage::catalog:
      s.title = entity == "spool" ? "My Spools" : "My Filaments";
      s.button({326, 4, 146, 44}, "SEARCH", TagAction::search);
      if (catalog_page["items"].size() == 0)
        s.body = offset > 0 ? "End of the list."
                 : query.empty()
                     ? (entity == "spool" ? "No spools in Spoolman yet."
                                          : "No filaments in Spoolman yet.")
                     : "No matches for \"" + query + "\".\nSearch looks at names and makers.";
      for (unsigned i = 0; i < 3 && row + i < catalog_page["items"].size(); ++i) {
        auto item = catalog_page["items"][row + i];
        std::string text = filament_name(item);
        if (entity == "spool")
          text = "#" + std::to_string(item["id"].as<int>()) + " " + text + "\n" +
                 (item["remaining_weight"].isNull()
                      ? std::string("Remaining weight unknown")
                      : grams_text(item["remaining_weight"]) + " remaining");
        else
          text += "\n" + std::string(item["material"] | "") + "  " + grams_text(item["weight"]);
        s.button({8, static_cast<std::int16_t>(58 + i * 62), 464, 56}, text,
                 static_cast<TagAction>(static_cast<int>(TagAction::row0) + i));
      }
      s.button({8, 266, 140, 46}, "PREV", TagAction::previous, row > 0 || offset > 0);
      s.button({156, 266, 168, 46}, "BACK", TagAction::back);
      s.button({332, 266, 140, 46}, "NEXT", TagAction::next,
               row + 3 < catalog_page["items"].size() || (catalog_page["has_more"] | false));
      break;
    case TagPage::selected:
      // Filaments only: a spool row opens the review.
      s.title = "Use this filament?";
      s.body = filament_name(selected) + "\n" + std::string(selected["material"] | "") + " - " +
               diameter_text(selected["diameter"]);
      back();
      primary("CREATE A SPOOL", TagAction::use);
      break;
    case TagPage::create:
      s.title = "Create physical spool";
      s.body = filament_name(selected);
      if (!create_hint.empty())
        s.body += "\n" + create_hint;
      s.button({16, 112, 448, 44}, "Initial: " + weight_text(initial) + " g  -  EDIT",
               TagAction::initial);
      s.button({16, 162, 448, 44}, "Remaining: " + weight_text(remaining) + " g  -  EDIT",
               TagAction::remaining);
      s.button({16, 212, 448, 44},
               tare_known ? "Empty spool: " + weight_text(tare) + " g  -  EDIT"
                          : std::string("Empty spool: NOT SET  -  EDIT"),
               TagAction::tare);
      back();
      primary("CREATE SPOOL", TagAction::create);
      break;
    case TagPage::created:
      // Normally passed straight through to the review (take_auto_preview);
      // shown only after BACK from that review, or if the preview was refused.
      s.title = "Spool created";
      s.body = filament_name(selected) + "\nSpool #" + std::to_string(selected["id"].as<int>()) +
               " is in Spoolman. The tag is not written yet.";
      back();
      primary("REVIEW TAG WRITE", TagAction::use);
      break;
    case TagPage::review: {
      const bool clearing = phase == "clear_preview";
      const bool unchanged = !clearing && (view["semantic_no_change"] | false);
      const int target = view["spool_id"] | 0;
      // The writer's own reading of the tag decides the warning: another
      // spool owns this UID (previous_spool_id), or the tag holds another
      // spool's data (repurpose). The screen's own USE FOR ANOTHER SPOOL
      // start adds to it.
      const int owner = view["previous_spool_id"] | 0;
      const int moving_from =
          owner > 0 ? owner : (from_spool > 0 && from_spool != target ? from_spool : 0);
      const bool repurpose = view["repurpose"] | false;
      const bool moving = !clearing && !unchanged && (moving_from > 0 || repurpose);
      s.title = clearing    ? "Erase this tag?"
                : unchanged ? "Tag already up to date"
                : moving    ? "Move this tag?"
                            : "Ready to write";
      if (clearing)
        s.body =
            "Erases the filament data on the tag and removes its link in Spoolman.\nThe spool itself stays in Spoolman.";
      else if (unchanged)
        s.body = filament_name(view["spool"]) + "\nNothing needs to be written.";
      else {
        s.body = filament_name(view["spool"]) + "\nSpool #" + std::to_string(target);
        if (moving_from > 0)
          s.body += "\nFrom spool #" + std::to_string(moving_from) + " to #" +
                    std::to_string(target) + ". Spool #" + std::to_string(moving_from) +
                    " stays in your inventory.";
        else if (repurpose)
          s.body += "\nThis tag holds another spool's data. Writing replaces it.";
        else
          s.body += "\nReplaces the filament data on the tag.";
        if (view["recovering_interrupted_write"] | false)
          s.body += "\nAn earlier write to this tag was interrupted. Writing again completes it.";
        const auto dropped = dropped_fields_text(view["dropped_fields"]);
        if (!dropped.empty())
          s.body += "\n" + dropped;
        const auto notices = preview_notices_text(view);
        if (!notices.empty())
          s.body += "\n" + notices;
      }
      if (!clearing) {
        s.art = true;
        s.art_rgb = view_spool_rgb(view);
      }
      back();
      primary(clearing    ? "ERASE TAG"
              : unchanged ? "NOTHING TO WRITE"
                          : "WRITE TAG",
              TagAction::write, !unchanged, clearing);
      break;
    }
    case TagPage::skip_review:
      s.title = "Skip recovery?";
      s.body =
          phase == "association_pending"
              ? "The tag is written, but Spoolman will not know it belongs to this spool.\nYou can link it again later from Manage tag."
          : phase == "unlink_pending"
              ? "The tag is erased, but Spoolman may still list it on the old spool."
              : "Only do this if that tag is lost or damaged.\nThe station will not be able to write or clear it again.";
      back();
      primary("SKIP RECOVERY", TagAction::write, true, true);
      break;
    case TagPage::choose:
      s.title = "Which spool is this?";
      for (std::size_t i = 0; i < 3 && i < candidates.size(); ++i)
        s.button({8, static_cast<std::int16_t>(58 + i * 62), 464, 56},
                 "#" + std::to_string(candidates[i].first) + " " + candidates[i].second,
                 static_cast<TagAction>(static_cast<int>(TagAction::row0) + static_cast<int>(i)));
      if (candidates.size() > 3)
        s.body = "More matches: choose in the browser.";
      back();
      break;
    case TagPage::progress: {
      if (physical_phase(phase) || phase == "reading") {
        s.title = phase == "clearing"  ? "Clearing tag"
                  : phase == "writing" ? "Writing tag"
                  : phase == "reading" ? "Reading tag"
                                       : "Checking tag";
        s.body = "Keep the tag on the reader.";
        const int total = view["total_blocks"] | 0, done = view["completed_blocks"] | 0;
        s.indicator = TagIndicator::spinner;
        if (total > 0) {
          s.body += "\n" + std::to_string(done) + " / " + std::to_string(total) + " blocks";
          s.indicator = TagIndicator::bar;
          s.total = static_cast<std::uint16_t>(std::min(total, 65535));
          s.done = static_cast<std::uint16_t>(std::clamp(done, 0, int(s.total)));
        }
        // Reading is not a physical change: it keeps the stall escape and
        // never strands a screen following another client's request.
        if (phase == "reading" && stalled)
          s.body += "\nThis is taking longer than usual.";
        if (phase == "reading" && (stalled || !waiting))
          s.button({8, 266, 464, 46}, "BACK TO HOME", TagAction::home);
        break;
      }
      const bool reading_wait = phase == "queued" && reading_request;
      s.title = phase == "associating"     ? "Saving link in Spoolman"
                : phase == "unlinking"     ? "Removing link in Spoolman"
                : reading_wait             ? "Reading tag"
                : phase == "loading_spool" ? "Checking Spoolman"
                                           : "Please wait";
      s.body = phase == "associating"     ? "The tag is already written."
               : phase == "unlinking"     ? "The tag is already erased."
               : reading_wait             ? "Keep the tag on the reader."
               : phase == "loading_spool" ? "Getting spool details from Spoolman..."
               : working.empty()          ? "Working on your request..."
                                          : working;
      s.indicator = TagIndicator::spinner;
      if (stalled)
        s.body += "\nThis is taking longer than usual.";
      // Another client's request (not waiting) must not strand this screen.
      // The writer's own link steps always end in a result and stay as is.
      if (stalled || (!waiting && phase != "associating" && phase != "unlinking"))
        s.button({8, 266, 464, 46}, "BACK TO HOME", TagAction::home);
      break;
    }
    case TagPage::reuse:
      s.title = "TAG READY TO REUSE";
      s.body = "The old filament information is removed.\nWhat should this tag become?";
      action(0, "CHOOSE EXISTING SPOOL", TagAction::spools);
      action(1, "NEW SPOOL FROM A FILAMENT", TagAction::filaments);
      s.button({8, 266, 464, 46}, "DONE", TagAction::home);
      break;
    case TagPage::ready:
      s.title = "TAG READY";
      s.body = filament_name(view["spool"]) + "\nSpool #" +
               std::to_string(view["spool_id"].as<int>()) + " - verified";
      s.art = true;
      s.art_rgb = view_spool_rgb(view);
      action(0, "WEIGH", TagAction::weigh);
      action(1, "ASSIGN TO PRINTER", TagAction::printer);
      // Wait for the internally refreshed physical/workflow identity before
      // navigating to pages that operate on the active spool.
      s.buttons[0].enabled = s.buttons[1].enabled =
          current_spool > 0 && current_spool == view["spool_id"].as<int>();
      if (!s.buttons[0].enabled)
        s.body += "\nRefreshing the active spool...";
      s.button({8, 266, 464, 46}, "DONE", TagAction::home);
      break;
    case TagPage::error:
      s.title = phase == "recovery_discarded" ? "Recovery skipped" : "Needs attention";
      s.body = plain_error(message);
      back();
      break;
  }
  if (waiting && page != TagPage::progress) {
    s.title = "Please wait";
    s.body = working.empty() ? "Working on your request..." : working;
    s.count = 0;
    s.art = false;
    s.indicator = TagIndicator::spinner;
    if (stalled && !physical_phase(phase)) {
      s.body += "\nThis is taking longer than usual.";
      s.button({8, 266, 464, 46}, "BACK TO HOME", TagAction::home);
    }
  }
  return s;
}
}  // namespace opentag::ui
