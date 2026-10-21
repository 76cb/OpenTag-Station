#pragma once
// 1.0.1-beta.5 touchscreen cases (#93): a tag of another technology (NFC-A)
// is found and named, nothing is read. Included by test_main.cpp after
// beta4_cases.hpp.
namespace beta5 {
using namespace opentag;
using namespace opentag::ui;
using rc23::mentions;

inline void nfca_tag_is_named_as_not_supported_yet() {
  TagFlow f;
  // The UI passes the NFCID1 as the uid and the kind as the error text.
  const std::string kind = "NTAG or other Type 2 tag (NFC-A)";
  f.observe_tag("04A1B2C3D4E5F6", &kind);
  f.observe_lifecycle(services::TagLifecycle::unsupported_type);
  const auto s = f.screen();
  TEST_ASSERT_TRUE(mentions(s, "Tag type not supported yet"));
  TEST_ASSERT_TRUE(mentions(s, "NTAG or other Type 2 tag (NFC-A)"));
  TEST_ASSERT_TRUE(mentions(s, "ID 04A1B2C3D4E5F6"));
  TEST_ASSERT_TRUE(mentions(s, "Use an OpenPrintTag on NXP ICODE SLIX2."));
  TEST_ASSERT_FALSE(mentions(s, "can't be used as it is"));
  // Only DONE: nothing can be written, linked or cleared.
  TEST_ASSERT_EQUAL(1, s.count);
  TEST_ASSERT_EQUAL_STRING("DONE", s.buttons[0].text.c_str());
  // Removing it returns to the empty prompt; an NFC-V tag after it is a new flow.
  f.observe_tag("", nullptr);
  f.observe_lifecycle(services::TagLifecycle::no_tag);
  TEST_ASSERT_TRUE(mentions(f.screen(), "PLACE A TAG"));
  f.observe_tag("E004000000000001", nullptr);
  f.observe_lifecycle(services::TagLifecycle::blank_compatible);
  TEST_ASSERT_TRUE(mentions(f.screen(), "BLANK TAG"));
}
}  // namespace beta5
