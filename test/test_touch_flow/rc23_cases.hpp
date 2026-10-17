#pragma once
// rc.23 touchscreen cases: weigh result ownership, dropped tag fields, plain
// tag action names, Spoolman lookup states and plain writer errors.
// Included by test_main.cpp after train_cases.hpp.
#include "ui/weigh_view.hpp"
namespace rc23 {
using namespace opentag;
using namespace opentag::ui;
using rc10::feed;
using rc10::shows;
inline bool mentions(const TagScreen& s, const char* text) { return s.body.find(text) != std::string::npos; }
inline const TagButton* labelled(const TagScreen& s, const char* text) {
  for (std::size_t i = 0; i < s.count; ++i)
    if (s.buttons[i].text == text) return &s.buttons[i];
  return nullptr;
}

inline services::WeighSyncSnapshot measurement(int spool, std::uint64_t generation, const char* phase) {
  services::WeighSyncSnapshot m;
  m.measurement_id = 9;
  m.generation = generation;
  m.spool_id = spool;
  m.name = "Old spool PLA";
  m.phase = phase;
  m.message = "Measured. Tap Update Spoolman to save it.";
  m.tare = 130.f;
  m.measured = 712.4f;
  m.canonical_remaining = 720.f;
  return m;
}

// P3-02: the Weigh page shows a result only for the spool now on the station.
inline void weigh_result_belongs_to_the_spool_on_the_station() {
  const auto m = measurement(28, 4, "ready");
  auto v = weigh_view(m, {true, 28, 4, "SUNLU PLA+"});
  TEST_ASSERT_TRUE(v.current);TEST_ASSERT_TRUE(v.show_receipt);TEST_ASSERT_TRUE(v.can_save);
  TEST_ASSERT_EQUAL_STRING("Old spool PLA", v.title.c_str());
  TEST_ASSERT_EQUAL_STRING("Tap UPDATE SPOOLMAN to set its remaining weight to 712 g.", v.message.c_str());
  TEST_ASSERT_EQUAL_STRING("WEIGH AGAIN", v.weigh_label);
  // Swapped for another spool (new generation and spool): nothing of the
  // previous result, and the save is not offered.
  v = weigh_view(m, {true, 31, 5, "Polymaker PETG"});
  TEST_ASSERT_FALSE(v.current);TEST_ASSERT_FALSE(v.show_receipt);TEST_ASSERT_FALSE(v.can_save);
  TEST_ASSERT_EQUAL_STRING("Polymaker PETG", v.title.c_str());
  TEST_ASSERT_EQUAL_STRING("WEIGH", v.weigh_label);
  TEST_ASSERT_EQUAL(std::string::npos, v.message.find("UPDATE SPOOLMAN"));
  // The same spool taken off and put back is a new identification.
  TEST_ASSERT_FALSE(weigh_view(m, {true, 28, 5, "SUNLU PLA+"}).can_save);
  // A spool resolved only after the measurement started.
  TEST_ASSERT_FALSE(weigh_view(measurement(0, 4, "unavailable"), {true, 28, 4, "SUNLU PLA+"}).current);
  // Station emptied.
  v = weigh_view(m, {false, 0, 6, ""});
  TEST_ASSERT_FALSE(v.show_receipt);TEST_ASSERT_EQUAL_STRING("Scale", v.title.c_str());
  TEST_ASSERT_EQUAL_STRING("Place a tagged spool on the station, then tap WEIGH.", v.message.c_str());
  // Already saved: the receipt stays, the save is not offered twice.
  auto saved = m;saved.consumed = true;saved.phase = "updated";saved.message = "Saved.";
  v = weigh_view(saved, {true, 28, 4, ""});
  TEST_ASSERT_TRUE(v.show_receipt);TEST_ASSERT_FALSE(v.can_save);TEST_ASSERT_EQUAL_STRING("Saved.", v.message.c_str());
  // No measurement yet.
  services::WeighSyncSnapshot none;
  v = weigh_view(none, {true, 28, 4, "SUNLU PLA+"});
  TEST_ASSERT_FALSE(v.current);TEST_ASSERT_EQUAL_STRING("Tap WEIGH to measure this spool.", v.message.c_str());
}
// B2-09: while a weight is being saved, WEIGH is refused and says why.
inline void weigh_is_refused_while_saving() {
  auto m = measurement(28, 4, "updating");m.consumed = true;
  auto v = weigh_view(m, {true, 28, 4, ""});
  TEST_ASSERT_TRUE(v.saving);TEST_ASSERT_FALSE(v.can_save);
  TEST_ASSERT_EQUAL_STRING("Saving to Spoolman...", v.message.c_str());
  v = weigh_view(m, {true, 31, 5, "PETG"});
  TEST_ASSERT_TRUE(v.saving);
  TEST_ASSERT_EQUAL_STRING("Saving the previous weight to Spoolman. Try again in a moment.", v.message.c_str());
  TEST_ASSERT_FALSE(weigh_view(measurement(28, 4, "ready"), {true, 28, 4, ""}).saving);
}

// P3-03: data the tag write leaves out is named on the review page.
inline void review_names_fields_that_are_not_written() {
  TagFlow f;
  feed(f, R"({"phase":"preview","spool_id":31,"dropped_fields":[{"field":"material","value":"PLA+ Silk Rainbow","reason":"too long"},{"field":"name","value":"x","reason":"too long"},{"field":"name","value":"y","reason":"again"}],"spool":{"filament":{"name":"PLA","color_hex":"1E90FF"}}})");
  auto s = f.screen();
  TEST_ASSERT_TRUE(mentions(s, "\nNot written: material, name"));
  TEST_ASSERT_EQUAL(std::string::npos, s.body.find("too long"));
  TEST_ASSERT_TRUE(s.art);
  const auto body = tag_body_box(s);
  TEST_ASSERT_TRUE(body.x + body.w <= layout::tag_art.x);
  feed(f, R"({"phase":"preview","spool_id":31,"previous_spool_id":28,"dropped_fields":[{"field":"vendor"},{"field":"material_type"},{"field":"article_number"},{"field":"color"},{"field":"new_thing"}]})");
  s = f.screen();
  TEST_ASSERT_EQUAL_STRING("Move this tag?", s.title.c_str());
  TEST_ASSERT_TRUE(mentions(s, "Not written: maker, material type, article number and 2 more"));
  for (const char* none : {R"({"phase":"preview","spool_id":31})", R"({"phase":"preview","spool_id":31,"dropped_fields":[]})",
                           R"({"phase":"clear_preview","dropped_fields":[{"field":"name"}]})"}) {
    feed(f, none);
    TEST_ASSERT_FALSE_MESSAGE(mentions(f.screen(), "Not written"), none);
  }
}

// UI-13: "Assign" is only for printer toolheads; tag actions say what they do.
inline void tag_actions_use_plain_names() {
  TagFlow f;f.lifecycle = services::TagLifecycle::blank_compatible;
  auto s = f.screen();
  TEST_ASSERT_NOT_NULL(labelled(s, "WRITE TAG"));TEST_ASSERT_NULL(labelled(s, "ASSIGN TAG"));
  f.act(TagAction::sources);TEST_ASSERT_EQUAL_STRING("Write tag", f.screen().title.c_str());
  TagFlow g;g.lifecycle = services::TagLifecycle::linked;g.current_spool = 28;
  s = g.screen();
  const auto* other = labelled(s, "USE FOR ANOTHER SPOOL");
  TEST_ASSERT_NOT_NULL(other);TEST_ASSERT_TRUE(other->action == TagAction::sources);
  TEST_ASSERT_NULL(labelled(s, "REASSIGN"));
  g.act(TagAction::sources);TEST_ASSERT_EQUAL_STRING("Use for another spool", g.screen().title.c_str());
  for (const auto& screen : {f.screen(), g.screen(), s})
    for (std::size_t i = 0; i < screen.count; ++i)
      TEST_ASSERT_EQUAL(std::string::npos, screen.buttons[i].text.find("ASSIGN"));
}

// B2-08: "not linked" only once Spoolman answered for this tag.
inline void unlinked_tag_waits_for_spoolman() {
  TagFlow f;f.lifecycle = services::TagLifecycle::unlinked;f.material = "PLA";
  f.lookup = SpoolLookup::resolving;
  auto s = f.screen();
  TEST_ASSERT_TRUE(mentions(s, "Looking up this spool in Spoolman..."));
  TEST_ASSERT_FALSE(mentions(s, "Not linked"));TEST_ASSERT_FALSE(shows(s, TagAction::sources));
  TEST_ASSERT_TRUE(shows(s, TagAction::home));
  f.lookup = SpoolLookup::unavailable;s = f.screen();
  TEST_ASSERT_TRUE(mentions(s, "Spoolman is not reachable. The tag's link cannot be checked right now."));
  TEST_ASSERT_TRUE(mentions(s, "It will be looked up again when Spoolman is back."));
  TEST_ASSERT_FALSE(mentions(s, "Not linked"));TEST_ASSERT_FALSE(shows(s, TagAction::sources));
  for (std::size_t i = 0; i < s.count; ++i) TEST_ASSERT_FALSE(s.buttons[i].primary);
  f.lookup = SpoolLookup::checked;s = f.screen();
  TEST_ASSERT_TRUE(mentions(s, "Not linked to a Spoolman spool yet"));
  TEST_ASSERT_TRUE(shows(s, TagAction::sources));
  // A linked tag is unaffected by the lookup state.
  TagFlow g;g.lifecycle = services::TagLifecycle::linked;g.current_spool = 4;g.lookup = SpoolLookup::unavailable;
  TEST_ASSERT_TRUE(shows(g.screen(), TagAction::update));
}

// UI-13: internal writer messages become actions; unknown ones are kept.
inline void writer_errors_are_shown_in_plain_words() {
  TagFlow f;feed(f, R"({"phase":"failed","message":"Owner instance UUID changed; unlink refused"})");
  auto s = f.screen();
  TEST_ASSERT_EQUAL(std::string::npos, s.body.find("UUID"));
  TEST_ASSERT_TRUE(mentions(s, "Check the spool in Spoolman"));
  TEST_ASSERT_EQUAL_STRING("A different tag is on the reader.", plain_error("A different tag is on the reader.").c_str());
  TEST_ASSERT_EQUAL(std::string::npos, plain_error("Preview workspace unavailable; no write authorized").find("workspace"));
  TEST_ASSERT_EQUAL(std::string::npos, plain_error("Multiple NFC UID owners; clean up Spoolman before continuing").find("UID"));
  TEST_ASSERT_EQUAL(std::string::npos, plain_error("Configure distinct Spoolman identity and NFC UID extra fields").find("UID"));
  f.operation_failed("Backend command queue is full");
  TEST_ASSERT_EQUAL_STRING("The station is busy. Try again in a moment.", f.screen().body.c_str());
}
}  // namespace rc23
