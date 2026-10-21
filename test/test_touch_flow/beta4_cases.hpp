#pragma once
// 1.0.1-beta.4 touchscreen cases (#85): the spool-confirmation notice and the
// writer's generic preview warnings (U18). Included by test_main.cpp after
// rc23_cases.hpp.
namespace beta4 {
using namespace opentag;
using namespace opentag::ui;
using rc10::feed;
using rc23::mentions;

// "Linked to spool #..." is shown on the tag page after a Which-spool choice
// until Spoolman reports the tag as linked or another tag arrives.
inline void confirmation_notice_is_shown_until_the_tag_is_linked() {
  TagFlow f;
  f.observe_tag("E004000000000001", nullptr);
  f.observe_lifecycle(services::TagLifecycle::unlinked);
  f.candidates = {{41, "Prusament PETG"}, {42, "Prusament PETG"}};
  f.act(TagAction::choose);
  TEST_ASSERT_TRUE(f.act(TagAction::row1).empty());
  TEST_ASSERT_EQUAL(42, f.confirm_spool_id);
  f.confirmation_submitted(42, true);
  // The station clears the candidates and looks the spool up.
  f.candidates.clear();
  f.lookup = SpoolLookup::resolving;
  auto s = f.screen();
  TEST_ASSERT_TRUE(mentions(s, "Linked to spool #42."));
  TEST_ASSERT_TRUE(mentions(s, "Looking up this spool in Spoolman"));
  // Still there once the lookup answered "not linked" (the station may be
  // behind), gone as soon as the tag is linked.
  f.lookup = SpoolLookup::checked;
  TEST_ASSERT_TRUE(mentions(f.screen(), "Linked to spool #42."));
  f.observe_lifecycle(services::TagLifecycle::linked);
  f.current_spool = 42;
  s = f.screen();
  TEST_ASSERT_TRUE(mentions(s, "Linked to spool #42"));
  TEST_ASSERT_FALSE(mentions(s, "Checking it in Spoolman"));
  // A refused submission says so instead; a different tag forgets it.
  f.observe_lifecycle(services::TagLifecycle::unlinked);
  f.confirmation_submitted(42, false);
  TEST_ASSERT_TRUE(mentions(f.screen(), "The station is busy"));
  f.observe_tag("E004000000000002", nullptr);
  TEST_ASSERT_FALSE(mentions(f.screen(), "The station is busy"));
  // The notice never leaks into a recovery prompt or a two-tag warning.
  f.confirmation_submitted(42, true);
  f.multiple_tags = true;
  TEST_ASSERT_FALSE(mentions(f.screen(), "Linked to spool"));
  f.multiple_tags = false;
  feed(f, R"({"phase":"write_recovery","message":"Place the same tag on the reader to finish writing it.","spool_id":31})");
  TEST_ASSERT_FALSE(mentions(f.screen(), "Linked to spool"));
}

// U18: the review page shows the writer's other warnings in plain words.
inline void review_shows_generic_preview_warnings() {
  TagFlow f;
  feed(f, R"({"phase":"preview","spool_id":31,"recovering_interrupted_write":true,"dropped_fields":[{"field":"vendor","value":"An Extremely Long Vendor Name GmbH","reason":"too long"}],
    "warnings":["Physical NFC UID association will move from the previous spool to this target spool after verification. The previous spool UUID is retained.",
      "Writing is not atomic. Keep this tag on the reader and power connected until complete.",
      "Full rewrite replaces tag-only metadata with canonical Spoolman fields; inspect Current versus Proposed. Mutable update preserves other fields.",
      "Vendor \"An Extremely Long Vendor Name GmbH\" is not written to the tag: too long.",
      "missing recommended gtin","missing recommended manufactured_date","missing recommended min_print_temperature","missing recommended max_print_temperature",
      "Density differs from the filament's material class."],"spool":{"filament":{"name":"PLA","color_hex":"1E90FF"}}})");
  const auto s = f.screen();
  TEST_ASSERT_TRUE(mentions(s, "\nAn earlier write to this tag was interrupted. Writing again completes it."));
  TEST_ASSERT_TRUE(mentions(s, "\nNot written: maker"));
  TEST_ASSERT_TRUE(mentions(s, "\nOptional data not set: gtin, manufactured date, min print temperature and 1 more"));
  TEST_ASSERT_TRUE(mentions(s, "\nDensity differs from the filament's material class."));
  // Already said by the review itself or by "Not written": not repeated.
  for (const char* repeated : {"not atomic", "Full rewrite", "association will move", "An Extremely Long Vendor Name GmbH"})
    TEST_ASSERT_FALSE_MESSAGE(mentions(s, repeated), repeated);
  TEST_ASSERT_EQUAL(0, preview_notices_text(JsonVariantConst()).size());
  TagFlow g;
  feed(g, R"({"phase":"preview","spool_id":31,"warnings":["Writing is not atomic.","Density differs.","Diameter differs."]})");
  TEST_ASSERT_TRUE(mentions(g.screen(), "\nDensity differs. (and 1 more)"));
  TEST_ASSERT_FALSE(mentions(g.screen(), "Optional data"));
  TEST_ASSERT_FALSE(mentions(g.screen(), "interrupted"));
  // Clearing and no-change previews stay as they are.
  feed(g, R"({"phase":"clear_preview","warnings":["Density differs."]})");
  TEST_ASSERT_FALSE(mentions(g.screen(), "Density differs"));
  feed(g, R"({"phase":"preview","spool_id":31,"semantic_no_change":true,"warnings":["missing recommended gtin"]})");
  TEST_ASSERT_FALSE(mentions(g.screen(), "Optional data"));
}
}  // namespace beta4
