#pragma once

namespace opentag::nfc {
// The RF technology a tag was found with. Only NFC-V tags are read; an NFC-A
// tag is detected and named so the UIs can say it is not supported yet (#93).
enum class Technology { none, nfcv, nfca };
inline const char* to_string(Technology technology) {
  switch (technology) {
    case Technology::nfcv: return "nfcv";
    case Technology::nfca: return "nfca";
    case Technology::none: break;
  }
  return "none";
}
// The wording the browser and the API have used for NFC-V since 1.0.
inline const char* technology_label(Technology technology) {
  return technology == Technology::nfca ? "NFC-A / ISO14443A" : "NFC-V / ISO15693";
}
}  // namespace opentag::nfc
