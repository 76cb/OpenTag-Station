#include "services/tag_writer_service.hpp"
#include "nfc/formats/openprinttag/material_json.hpp"
#include "nfc/protocols/nfcv/read_protocol.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace opentag::services {
namespace {
using Result = core::Result<void>;
Result fail(const char *text) {
  return Result::failure({core::ErrorCategory::conflict, text, false});
}
std::string quote(const std::string &value) {
  network::BackendDocument d;
  d.set(value);
  std::string s;
  serializeJson(d, s);
  return s;
}
std::string encode(const std::string &s) {
  const char *hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')
      out += c;
    else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}
std::string scalar(JsonVariantConst value) {
  if (!value.is<const char *>())
    return {};
  network::BackendDocument d;
  if (deserializeJson(d, value.as<const char *>()) || !d.is<const char *>())
    return {};
  return d.as<std::string>();
}
bool same(JsonVariantConst a, JsonVariantConst b) {
  if (a.isNull() || b.isNull())
    return a.isNull() && b.isNull();
  if (a.is<double>() && b.is<double>())
    return a.as<double>() == b.as<double>();
  return a == b;
}
bool text_bound(JsonVariantConst v, std::size_t n, bool required = false) {
  return (!required && v.isNull()) ||
         (v.is<const char *>() && std::strlen(v.as<const char *>()) <= n &&
          (!required || *v.as<const char *>()));
}
std::string normalized_uid(const std::string &value) {
  std::string result;
  for (char c : value) {
    if (c == ':' || c == '-')
      continue;
    if (c >= 'a' && c <= 'f')
      c -= 'a' - 'A';
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')))
      return {};
    result += c;
  }
  return result.size() == 16 ? result : std::string{};
}
bool uid_cleared(JsonVariantConst value) {
  if (value.isNull())
    return true;
  if (!value.is<const char *>())
    return false;
  network::BackendDocument decoded;
  if (deserializeJson(decoded, value.as<const char *>()))
    return false;
  return decoded.isNull() ||
         (decoded.is<const char *>() && !*decoded.as<const char *>());
}
bool edit_field(const std::string &key, JsonVariantConst value, bool filament,
                bool expected = false) {
  const auto contains = [&](const char *keys) {
    return key.find('|') == std::string::npos &&
           std::string(keys).find("|" + key + "|") != std::string::npos;
  };
  if (contains(filament ? "|name|material|article_number|color_hex|"
                        : "|location|lot_nr|comment|")) {
    if (expected && value.isNull())
      return true;
    if (!value.is<const char *>() ||
        value.as<JsonString>().size() > (key == "comment" ? 1024U : 64U))
      return false;
    if (key != "color_hex")
      return true;
    const std::string color = value.as<std::string>();
    return (color.size() == 6 || color.size() == 8) &&
           color.find_first_not_of("0123456789abcdefABCDEF") ==
               std::string::npos;
  }
  if (!contains(filament ? "|weight|density|diameter|spool_weight|settings_"
                           "extruder_temp|settings_bed_temp|"
                         : "|initial_weight|used_weight|spool_weight|price|"))
    return false;
  if (expected && value.isNull())
    return key != "used_weight" && key != "density" && key != "diameter";
  if (!value.is<double>())
    return false;
  const double n = value.as<double>();
  const double maximum = key == "density"                  ? 30
                         : key == "diameter"               ? 10
                         : key == "settings_extruder_temp" ? 500
                         : key == "settings_bed_temp"      ? 200
                         : key == "price"                  ? 1000000
                                                           : 100000;
  if (!std::isfinite(n) || n < 0 || n > maximum)
    return false;
  if ((key == "density" || key == "diameter" || key == "weight") && n == 0)
    return false;
  return (key != "settings_extruder_temp" && key != "settings_bed_temp") ||
         value.is<int>();
}
bool canonical_record(JsonVariantConst record, int id, bool filament) {
  if (!record.is<JsonObjectConst>() || !record["id"].is<int>() ||
      record["id"].as<int>() != id)
    return false;
  const auto material = filament ? record : record["filament"];
  return material["id"].is<int>() && material["id"].as<int>() > 0 &&
         edit_field("density", material["density"], true) &&
         edit_field("diameter", material["diameter"], true) &&
         (filament || edit_field("used_weight", record["used_weight"], false));
}
std::uint32_t backend_identity(const config::SpoolmanSettings &s) {
  const auto identity =
      s.url + "\n" + s.identity_field + "\n" + s.nfc_uid_field;
  return nfc::nfcv::diagnostic_checksum(
      reinterpret_cast<const std::uint8_t *>(identity.data()), identity.size());
}
} // namespace
core::Result<network::BackendDocument>
TagWriterService::api(const char *method, const std::string &path,
                      JsonVariantConst body) {
  std::string bytes;
  if (!body.isNull())
    serializeJson(body, bytes);
  if (bytes.size() > 4096)
    return core::Result<network::BackendDocument>::failure(
        {core::ErrorCategory::configuration,
         "Writer backend payload exceeds bound", false});
  auto response = spoolman_.request(method, path, bytes, 24576);
  if (!response.ok())
    return core::Result<network::BackendDocument>::failure(response.error());
  return network::parse_backend_json(response.value().body, "Spoolman writer");
}
void TagWriterService::publish(const char *phase, const char *message,
                               std::size_t done, std::size_t total) {
  view_["phase"] = phase;
  view_["message"] = message;
  view_["completed_blocks"] = done;
  view_["total_blocks"] = total;
  network::ResponseBody out(24576);
  if (!view_.overflowed())
    serializeJson(view_, out);
  if (view_.overflowed() || out.failed() || out.overflowed()) {
    network::ResponseBody error(
        R"({"phase":"failed","message":"Writer snapshot workspace unavailable; retry"})");
    publish_(error);
  } else
    publish_(out);
}
Result TagWriterService::catalog(JsonObjectConst c) {
  const std::string entity = c["entity"] | "spool";
  if (entity != "vendor" && entity != "filament" && entity != "spool")
    return fail("Unsupported catalog entity");
  if (!text_bound(c["search"], 64) || !text_bound(c["material"], 64) ||
      !text_bound(c["article_number"], 64))
    return fail("Search exceeds bounds");
  if (!c["offset"].is<unsigned>() || c["offset"].as<unsigned>() > 1000000)
    return fail("Invalid catalog offset");
  const auto offset = c["offset"].as<unsigned>();
  std::string path = "/" + entity +
                     "?limit=8&offset=" + std::to_string(offset) +
                     "&sort=id:asc";
  const std::string prefix = entity == "spool" ? "filament." : "";
  const std::string search = c["search"] | "";
  if (!search.empty())
    path += "&" + prefix + "name=" + encode(search);
  if (entity != "vendor") {
    if (c["vendor_id"].as<int>() > 0)
      path += "&" + prefix +
              "vendor.id=" + std::to_string(c["vendor_id"].as<int>());
    for (const auto *key : {"material", "article_number"})
      if (c[key].is<const char *>() && *c[key].as<const char *>() &&
          (entity == "filament" || std::string(key) == "material"))
        path += "&" + prefix + key + "=" + encode(c[key].as<std::string>());
    if (entity == "spool" && c["filament_id"].as<int>() > 0)
      path += "&filament.id=" + std::to_string(c["filament_id"].as<int>());
  }
  auto page = api("GET", path);
  if (!page.ok())
    return Result::failure(page.error());
  if (!page.value().is<JsonArray>() || page.value().size() > 8)
    return fail("Spoolman pagination contract changed");
  for (auto item : page.value().as<JsonArrayConst>())
    if (!item["id"].is<int>() || item["id"].as<int>() <= 0)
      return fail("Malformed Spoolman catalog item");
  view_.clear();
  view_["entity"] = entity;
  view_["offset"] = offset;
  view_["next_offset"] = offset + page.value().size();
  view_["has_more"] = page.value().size() == 8;
  view_["items"].set(page.value());
  publish("catalog");
  return Result::success();
}
Result TagWriterService::import_preview(JsonObjectConst c) {
  auto source = c["entry"].as<JsonObjectConst>();
  if (source.isNull() ||
      c["contract"].as<std::string>() != "spoolmandb-community/0a39c9b5")
    return fail("Unsupported Community contract");
  if (!text_bound(source["id"], 180, true) ||
      !text_bound(source["manufacturer"], 64, true) ||
      !text_bound(source["name"], 128, true) ||
      !text_bound(source["material"], 64, true))
    return fail("Malformed/oversized Community identity");
  for (auto pair : source) {
    const std::string k = pair.key().c_str();
    const std::string allowed =
        "|id|manufacturer|name|material|density|weight|spool_weight|spool_type|"
        "is_refill|diameter|color_hex|color_hexes|extruder_temp|extruder_temp_"
        "range|bed_temp|bed_temp_range|fill|finish|multi_color_direction|"
        "pattern|translucent|glow|codes|eans|eans_refill|country_of_origin|sds_"
        "url|tds_url|";
    if (allowed.find("|" + k + "|") == std::string::npos)
      return fail("Community source format drift");
    if (pair.value().is<const char *>() && !text_bound(pair.value(), 512))
      return fail("Oversized Community field");
  }
  for (const auto *key : {"density", "diameter"})
    if (!source[key].is<double>() || !std::isfinite(source[key].as<double>()) ||
        source[key].as<double>() <= 0)
      return fail(
          "Community density and diameter must be positive for Spoolman");
  import_.clear();
  import_vendor_ = import_filament_ = 0;
  auto target = import_.to<JsonObject>();
  for (const auto *key :
       {"name", "material", "density", "diameter", "weight", "spool_weight",
        "color_hex", "multi_color_direction"}) {
    if (!source[key].isNull())
      target[key].set(source[key]);
  }
  if (!c["import_name"].isNull())
    target["name"] = c["import_name"];
  if (!text_bound(target["name"], 64, true))
    return fail("Choose a Spoolman display name of at most 64 bytes; source "
                "name remains in the preview");
  for (const auto *key : {"weight", "spool_weight"})
    if (!source[key].isNull() && (!source[key].is<double>() ||
                                  !std::isfinite(source[key].as<double>()) ||
                                  source[key].as<double>() < 0))
      return fail("Invalid Community weight");
  if (source["weight"].as<double>() == 0)
    target.remove("weight"); // Spoolman requires positive or unknown.
  if (!text_bound(source["color_hex"], 8))
    return fail("Malformed Community color");
  if (source["color_hex"].is<const char *>()) {
    auto s = source["color_hex"].as<std::string>();
    if ((s.size() != 6 && s.size() != 8) ||
        s.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
      return fail("Malformed Community color");
  }
  for (const auto *key : {"extruder_temp", "bed_temp"})
    if (!source[key].isNull()) {
      if (!source[key].is<int>() || source[key].as<int>() < 0 ||
          source[key].as<int>() > 1000)
        return fail("Invalid Community temperature");
      target[std::string("settings_") + (std::string(key) == "extruder_temp"
                                             ? "extruder_temp"
                                             : "bed_temp")] = source[key];
    }
  if (source["codes"].is<JsonArrayConst>() && source["codes"].size()) {
    if (!text_bound(source["codes"][0], 64, true))
      return fail("Community article exceeds Spoolman bound");
    target["article_number"] = source["codes"][0];
  }
  if (source["color_hexes"].is<JsonArrayConst>()) {
    if (source["color_hexes"].size() > 6)
      return fail("Too many Community colors");
    std::string colors;
    for (auto v : source["color_hexes"].as<JsonArrayConst>()) {
      if (!text_bound(v, 8, true))
        return fail("Malformed Community colors");
      if (!colors.empty())
        colors += ',';
      colors += v.as<std::string>();
    }
    if (!colors.empty()) {
      target["multi_color_hexes"] = colors;
      target.remove("color_hex");
    }
  }
  const std::string external =
      "spoolmandb-community:" + source["id"].as<std::string>();
  target["external_id"] = external;
  std::string provenance =
      "SpoolmanDB Community: " + source["id"].as<std::string>();
  for (const auto *key :
       {"extruder_temp_range", "bed_temp_range", "spool_type", "is_refill",
        "fill", "finish", "pattern", "translucent", "glow", "country_of_origin",
        "sds_url", "tds_url"}) {
    if (source[key].isNull())
      continue;
    std::string value;
    serializeJson(source[key], value);
    if (provenance.size() + std::strlen(key) + value.size() + 3 <= 1024)
      provenance += "\n" + std::string(key) + ": " + value;
  }
  target["comment"] = provenance;
  if (import_.overflowed())
    return fail(
        "Community import workspace unavailable; no remote records created");
  auto vendors =
      api("GET", "/vendor?name=" +
                     encode(quote(source["manufacturer"].as<std::string>())) +
                     "&limit=2&offset=0");
  if (!vendors.ok())
    return Result::failure(vendors.error());
  if (!vendors.value().is<JsonArray>() || vendors.value().size() > 1)
    return fail(
        "Ambiguous matching Spoolman vendors; resolve duplicates first");
  if (vendors.value().size())
    import_vendor_ = vendors.value()[0]["id"].as<int>();
  if (vendors.value().size() && import_vendor_ <= 0)
    return fail("Malformed matching Spoolman vendor");
  auto existing =
      api("GET", "/filament?external_id=" + encode(quote(external)) +
                     "&limit=2&offset=0");
  if (!existing.ok())
    return Result::failure(existing.error());
  if (!existing.value().is<JsonArray>() || existing.value().size() > 1)
    return fail("Duplicate Community source identity in Spoolman");
  if (existing.value().size())
    import_filament_ = existing.value()[0]["id"].as<int>();
  if (existing.value().size() && import_filament_ <= 0)
    return fail("Malformed matching Spoolman filament");
  // Deterministic equivalent matching, all pages; do not equate similar names.
  if (!import_filament_ && import_vendor_) {
    for (unsigned offset = 0;; offset += 8) {
      if (offset > 100000)
        return fail("Duplicate check exceeded bound; narrow catalog first");
      auto page =
          api("GET",
              "/filament?vendor.id=" + std::to_string(import_vendor_) +
                  "&name=" + encode(quote(target["name"].as<std::string>())) +
                  "&limit=8&offset=" + std::to_string(offset) + "&sort=id:asc");
      if (!page.ok())
        return Result::failure(page.error());
      if (!page.value().is<JsonArray>() || page.value().size() > 8)
        return fail("Invalid duplicate-check page");
      for (auto item : page.value().as<JsonArrayConst>()) {
        bool equivalent = true;
        for (const auto *key :
             {"name", "material", "diameter", "density", "weight",
              "spool_weight", "color_hex", "multi_color_hexes",
              "multi_color_direction", "article_number",
              "settings_extruder_temp", "settings_bed_temp"})
          equivalent = equivalent && same(item[key], target[key]);
        if (equivalent) {
          if (item["id"].as<int>() <= 0)
            return fail("Malformed equivalent Spoolman filament");
          if (import_filament_)
            return fail("Multiple equivalent Spoolman filaments");
          import_filament_ = item["id"].as<int>();
        }
      }
      if (page.value().size() < 8)
        break;
    }
  }
  view_.clear();
  view_["vendor_name"] = source["manufacturer"];
  view_["vendor_id"] = import_vendor_;
  view_["filament_id"] = import_filament_;
  view_["proposed_filament"].set(import_);
  view_["source"].set(source);
  import_token_ = external + ":" + std::to_string(++preview_serial_);
  view_["import_token"] = import_token_;
  publish("import_preview",
          "Confirm vendor/filament creation or reuse before import");
  return Result::success();
}
Result TagWriterService::import_commit(JsonObjectConst c) {
  if (import_token_.empty() ||
      c["import_token"].as<std::string>() != import_token_ ||
      view_["phase"].as<std::string>() != "import_preview")
    return fail("Import preview changed; preview again");
  // Recheck matching data immediately before create. The backend owner
  // serializes local imports; external Spoolman edits remain a documented
  // concurrency limit.
  network::BackendDocument refresh;
  refresh["entry"].set(view_["source"]);
  refresh["import_name"] = view_["proposed_filament"]["name"];
  refresh["contract"] = "spoolmandb-community/0a39c9b5";
  if (refresh.overflowed())
    return fail("Import confirmation workspace unavailable");
  auto checked = import_preview(refresh.as<JsonObjectConst>());
  if (!checked.ok())
    return checked;
  if (!import_vendor_) {
    network::BackendDocument body;
    body["name"] = view_["vendor_name"];
    body["external_id"] =
        "spoolmandb-community:" + view_["vendor_name"].as<std::string>();
    if (body.overflowed())
      return fail("Vendor creation workspace unavailable; no request sent");
    auto created = api("POST", "/vendor", body.as<JsonVariantConst>());
    if (!created.ok())
      return Result::failure(created.error());
    import_vendor_ = created.value()["id"].as<int>();
  }
  if (import_vendor_ <= 0)
    return fail("Vendor creation did not return an ID");
  auto vendor = api("GET", "/vendor/" + std::to_string(import_vendor_));
  if (!vendor.ok())
    return Result::failure(vendor.error());
  if (vendor.value()["id"].as<int>() != import_vendor_)
    return fail("Vendor readback mismatch");
  if (!import_filament_) {
    import_["vendor_id"] = import_vendor_;
    if (import_.overflowed())
      return fail("Filament creation workspace unavailable; no request sent");
    auto created = api("POST", "/filament", import_.as<JsonVariantConst>());
    if (!created.ok())
      return Result::failure(created.error());
    import_filament_ = created.value()["id"].as<int>();
  }
  if (import_filament_ <= 0)
    return fail("Filament creation did not return an ID");
  auto canonical = api("GET", "/filament/" + std::to_string(import_filament_));
  if (!canonical.ok())
    return Result::failure(canonical.error());
  if (canonical.value()["id"].as<int>() != import_filament_)
    return fail("Filament canonical readback mismatch");
  view_.clear();
  view_["filament"].set(canonical.value());
  import_token_.clear();
  publish("imported",
          "Canonical Spoolman filament ready; select or create a spool");
  return Result::success();
}
Result TagWriterService::create_spool(JsonObjectConst c) {
  auto fields = c["spool"].as<JsonObjectConst>();
  if (!fields["filament_id"].is<int>() || fields["filament_id"].as<int>() <= 0)
    return fail("Select a canonical Spoolman filament");
  network::BackendDocument body;
  body["filament_id"] = fields["filament_id"];
  for (auto field : fields) {
    const std::string k = field.key().c_str();
    if (k == "filament_id")
      continue;
    if (k == "initial_weight" || k == "remaining_weight" ||
        k == "used_weight" || k == "spool_weight" || k == "price") {
      if (!field.value().is<double>() ||
          !std::isfinite(field.value().as<double>()) ||
          field.value().as<double>() < 0)
        return fail("Spool weights and price must be nonnegative");
    } else if (k == "location" || k == "lot_nr" || k == "comment") {
      if (!text_bound(field.value(), k == "comment" ? 1024 : 64))
        return fail("Spool field exceeds bound");
    } else
      return fail("Unsupported spool field");
    body[k] = field.value();
  }
  if (!fields["remaining_weight"].isNull() && !fields["used_weight"].isNull())
    return fail("Supply remaining OR used weight, not both");
  if (body.overflowed())
    return fail("Spool creation workspace unavailable; no request sent");
  auto filament = api(
      "GET", "/filament/" + std::to_string(fields["filament_id"].as<int>()));
  if (!filament.ok())
    return Result::failure(filament.error());
  if (filament.value()["id"].as<int>() != fields["filament_id"].as<int>())
    return fail("Canonical filament ID mismatch; no spool created");
  auto created = api("POST", "/spool", body.as<JsonVariantConst>());
  if (!created.ok())
    return Result::failure(created.error());
  auto id = created.value()["id"].as<int>();
  if (id <= 0)
    return fail("Spool creation returned invalid ID");
  auto canonical = api("GET", "/spool/" + std::to_string(id));
  if (!canonical.ok())
    return Result::failure(canonical.error());
  if (canonical.value()["id"].as<int>() != id)
    return fail("Spool canonical readback mismatch");
  view_.clear();
  view_["spool"].set(canonical.value());
  publish("spool_selected", "Canonical spool created; preview before writing");
  return Result::success();
}
Result TagWriterService::edit_record(JsonObjectConst c, bool filament) {
  // Edits never invoke the reader or writer. Pending associations are rejected
  // by process() before reaching here. Any edit invalidates the old preview.
  plan_.reset();
  view_.clear();
  const auto id_value = c[filament ? "filament_id" : "spool_id"];
  if (!id_value.is<int>() || id_value.as<int>() <= 0)
    return fail("Edit requires an exact positive record ID");
  const int id = id_value.as<int>();
  const auto changes = c["changes"].as<JsonObjectConst>();
  if (changes.isNull() || changes.size() == 0 || changes.size() > 11)
    return fail("Supply explicit changed fields");
  for (auto field : changes)
    if (!edit_field(field.key().c_str(), field.value(), filament))
      return fail("Unsupported edit field, type or value outside safe bounds");
  const auto expected = c["expected"].as<JsonObjectConst>();
  if (expected.isNull() || expected.size() > 11)
    return fail("Edit requires the reviewed field values in expected");
  for (auto field : expected)
    if (!edit_field(field.key().c_str(), field.value(), filament, true))
      return fail("Unsupported expected field, type or value");
  for (auto field : changes)
    if (!expected.containsKey(field.key().c_str()))
      return fail("Expected must include every changed field");
  if (filament && !c["spool_id"].isNull() &&
      (!c["spool_id"].is<int>() || c["spool_id"].as<int>() <= 0))
    return fail("Invalid selected spool ID");
  const auto path =
      std::string(filament ? "/filament/" : "/spool/") + std::to_string(id);
  publish("editing",
          "Saving canonical Spoolman data; a fresh tag preview is required");
  network::BackendDocument patch;
  {
    auto before = api("GET", path);
    if (!before.ok())
      return Result::failure(before.error());
    if (!canonical_record(before.value().as<JsonVariantConst>(), id, filament))
      return fail("Malformed canonical record; nothing patched");
    // Compare the entire edit before constructing or sending any PATCH. Missing
    // optional values and explicit null both mean unknown; zero is a real
    // value.
    for (auto field : changes)
      if (!same(before.value()[field.key()], expected[field.key()])) {
        view_[filament ? "filament" : "spool"].set(before.value());
        view_["edit_conflict"] = true;
        return fail("Spoolman changed since this editor was opened. Refresh "
                    "and review again.");
      }
    for (auto field : changes)
      if (!same(before.value()[field.key()], field.value()))
        patch[field.key()] = field.value();
  }
  if (filament && c["spool_id"].is<int>()) {
    auto selected =
        api("GET", "/spool/" + std::to_string(c["spool_id"].as<int>()));
    if (!selected.ok())
      return Result::failure(selected.error());
    if (!canonical_record(selected.value().as<JsonVariantConst>(),
                          c["spool_id"].as<int>(), false) ||
        selected.value()["filament"]["id"].as<int>() != id)
      return fail(
          "Selected spool no longer uses this filament; refresh selection");
  }
  if (patch.overflowed())
    return fail("Edit workspace unavailable; nothing patched");
  if (patch.size()) {
    auto updated = api("PATCH", path, patch.as<JsonVariantConst>());
    if (!updated.ok())
      return Result::failure(updated.error());
  }
  {
    auto after = api("GET", path);
    if (!after.ok())
      return Result::failure(after.error());
    if (!canonical_record(after.value().as<JsonVariantConst>(), id, filament))
      return fail("Malformed canonical edit readback; refresh before retrying");
    for (auto field : changes)
      if (!same(after.value()[field.key()], field.value()))
        return fail("Spoolman edit readback mismatch; refresh before retrying");
    view_[filament ? "filament" : "spool"].set(after.value());
  }
  if (filament && c["spool_id"].is<int>()) {
    auto selected =
        api("GET", "/spool/" + std::to_string(c["spool_id"].as<int>()));
    if (!selected.ok()) {
      view_.clear();
      return Result::failure(selected.error());
    }
    if (!canonical_record(selected.value().as<JsonVariantConst>(),
                          c["spool_id"].as<int>(), false) ||
        selected.value()["filament"]["id"].as<int>() != id) {
      view_.clear();
      return fail("Selected spool readback changed; refresh before previewing");
    }
    for (auto field : changes)
      if (!same(selected.value()["filament"][field.key()], field.value())) {
        view_.clear();
        return fail("Selected spool has stale filament readback; refresh "
                    "before previewing");
      }
    view_["spool"].set(selected.value());
  }
  if (view_.overflowed() || measureJson(view_) > 24000) {
    view_.clear();
    return fail("Edit readback exceeds workspace; refresh before previewing");
  }
  publish("updated",
          "Saved and verified in Spoolman. Generate a new tag preview.");
  return Result::success();
}
Result TagWriterService::unique_identity() {
  const auto quoted = quote(uuid_);
  if (quoted.size() != uuid_.size() + 2)
    return fail("Identity query workspace unavailable");
  auto matches =
      api("GET", "/spool?allow_archived=true&limit=2&offset=0&extra." +
                     encode(identity_key_) + "=" + encode(quoted));
  if (!matches.ok())
    return Result::failure(matches.error());
  if (!matches.value().is<JsonArray>() || matches.value().size() > 1)
    return fail("Duplicate instance UUID conflict");
  if (matches.value().size() && matches.value()[0]["id"].as<int>() != spool_id_)
    return fail("Instance UUID belongs to another Spoolman spool");
  return Result::success();
}
core::Result<std::int32_t> TagWriterService::uid_owner() {
  std::int32_t owner = 0;
  const auto physical = plan_->uid.hex();
  // Existing external clients may store separated or lower-case UIDs. Query
  // each bounded exact spelling and deduplicate IDs across those responses.
  for (char separator : {'\0', ':', '-'}) {
    std::string value;
    for (std::size_t i = 0; i < physical.size(); ++i) {
      if (separator && i && i % 2 == 0)
        value += separator;
      value += physical[i];
    }
    for (bool lower : {false, true}) {
      if (lower)
        for (auto &ch : value)
          if (ch >= 'A' && ch <= 'F')
            ch += 'a' - 'A';
      const auto quoted = quote(value);
      if (quoted.size() != value.size() + 2)
        return core::Result<std::int32_t>::failure(
            fail("NFC UID query workspace unavailable").error());
      auto matches =
          api("GET", "/spool?allow_archived=true&limit=2&offset=0&extra." +
                         encode(uid_key_) + "=" + encode(quoted));
      if (!matches.ok())
        return core::Result<std::int32_t>::failure(matches.error());
      if (!matches.value().is<JsonArray>() || matches.value().size() > 1)
        return core::Result<std::int32_t>::failure(
            fail("Multiple NFC UID owners; clean up Spoolman before continuing")
                .error());
      if (!matches.value().size())
        continue;
      const auto id = matches.value()[0]["id"].as<int>();
      if (id <= 0 || (owner && owner != id))
        return core::Result<std::int32_t>::failure(
            fail("Conflicting/malformed NFC UID ownership").error());
      owner = id;
    }
  }
  return core::Result<std::int32_t>::success(owner);
}
Result TagWriterService::prepare_uid_owner() {
  auto owner = uid_owner();
  if (!owner.ok())
    return Result::failure(owner.error());
  plan_->previous_spool_id = owner.value() == spool_id_ ? 0 : owner.value();
  return Result::success();
}
Result TagWriterService::clear_previous_uid() {
  auto owner = uid_owner();
  if (!owner.ok())
    return Result::failure(owner.error());
  if (owner.value() && owner.value() != spool_id_ &&
      owner.value() != plan_->previous_spool_id)
    return fail("NFC UID owner changed after confirmation; association "
                "requires review");
  if (!plan_->previous_spool_id)
    return Result::success();
  const auto path = "/spool/" + std::to_string(plan_->previous_spool_id);
  auto previous = api("GET", path);
  if (!previous.ok())
    return Result::failure(previous.error());
  if (previous.value()["id"].as<int>() != plan_->previous_spool_id)
    return fail("Previous spool readback ID mismatch");
  if (uid_cleared(previous.value()["extra"][uid_key_]))
    return Result::success();
  if (normalized_uid(scalar(previous.value()["extra"][uid_key_])) !=
      plan_->uid.hex())
    return fail(
        "Previous spool NFC UID changed; refusing to clear a different tag");
  network::BackendDocument patch;
  patch["extra"][uid_key_] = nullptr;
  if (patch.overflowed())
    return fail("Previous UID cleanup workspace unavailable");
  auto cleared = api("PATCH", path, patch.as<JsonVariantConst>());
  if (!cleared.ok())
    return Result::failure(cleared.error());
  auto verified = api("GET", path);
  if (!verified.ok())
    return Result::failure(verified.error());
  if (verified.value()["id"].as<int>() != plan_->previous_spool_id ||
      !uid_cleared(verified.value()["extra"][uid_key_]))
    return fail("Previous NFC UID cleanup readback failed; target not patched");
  return Result::success();
}
Result TagWriterService::prepare(JsonObjectConst c) {
  if (association_pending_)
    return fail("Retry the pending association before preparing another tag");
  auto previous = std::move(plan_);
  std::int32_t journal_spool = spool_id_;
  std::uint32_t journal_backend = backend_identity(spoolman_.settings_);
  if (journal_) {
    auto recorded = network::make_external<nfc::WriterPlan>(
        [] { return nfc::WriterPlan{}; });
    if (!recorded) {
      plan_ = std::move(previous);
      return fail("Recovery journal workspace unavailable");
    }
    if (journal_->load(*recorded, journal_spool, journal_backend))
      previous = std::move(recorded);
  }
  plan_ =
      network::make_external<nfc::WriterPlan>([] { return nfc::WriterPlan{}; });
  if (!plan_) {
    plan_ = std::move(previous);
    return fail("PSRAM writer workspace unavailable");
  }
  view_.clear();
  publish("reading", "Reading complete tag and protection state");
  auto read = writer_.read(*plan_, previous.get());
  if (!read.ok()) {
    plan_ = std::move(previous);
    return read;
  }
  if (plan_->journal_state == nfc::WriterPlan::JournalState::target &&
      journal_spool > 0) {
    if (journal_backend != backend_identity(spoolman_.settings_))
      return fail("Recovery record belongs to different Spoolman settings; "
                  "restore settings first");
    if (!plan_->current.material.instance_uuid)
      return fail("Recovery target has no instance identity");
    spool_id_ = journal_spool;
    uuid_ = nfc::openprinttag::instance_uuid_text(
        *plan_->current.material.instance_uuid);
    settings_url_ = spoolman_.settings_.url;
    identity_key_ = spoolman_.settings_.identity_field;
    uid_key_ = spoolman_.settings_.nfc_uid_field;
    plan_->previous_spool_id = previous->previous_spool_id;
    auto valid = writer_.plan(*plan_);
    if (!valid.ok())
      return valid;
    plan_->verified = true;
    association_pending_ = true;
    view_["spool_id"] = spool_id_;
    view_["instance_uuid"] = uuid_;
    view_["uid"] = plan_->uid.hex();
    view_["previous_spool_id"] = plan_->previous_spool_id;
    publish("association_pending", "Recovered complete physical target; retry "
                                   "Spoolman association without rewriting");
    return Result::success();
  }
  spool_id_ = c["spool_id"].as<int>();
  if (spool_id_ <= 0)
    return fail("Select a Spoolman spool");
  settings_url_ = spoolman_.settings_.url;
  identity_key_ = spoolman_.settings_.identity_field;
  uid_key_ = spoolman_.settings_.nfc_uid_field;
  if (identity_key_.empty() || uid_key_.empty() || identity_key_ == uid_key_)
    return fail(
        "Configure distinct Spoolman identity and NFC UID extra fields");
  publish("loading_spool", "Reading canonical Spoolman data after tag read");
  auto definitions = spoolman_.list_extra_fields();
  if (!definitions.ok())
    return Result::failure(definitions.error());
  for (const auto &key : {identity_key_, uid_key_}) {
    auto f = std::find_if(definitions.value().begin(),
                          definitions.value().end(), [&](const auto &v) {
                            return v.key == key &&
                                   v.kind == integrations::ExtraFieldKind::text;
                          });
    if (f == definitions.value().end())
      return fail(
          "Configured identity fields must exist as Spoolman text fields");
  }
  auto spool = api("GET", "/spool/" + std::to_string(spool_id_));
  if (!spool.ok())
    return Result::failure(spool.error());
  if (spool.value()["id"].as<int>() != spool_id_)
    return fail("Canonical spool ID mismatch");
  uuid_ = scalar(spool.value()["extra"][identity_key_]);
  if (uuid_.empty() && plan_->current.material.instance_uuid &&
      scalar(spool.value()["extra"][uid_key_]) == plan_->uid.hex())
    uuid_ = nfc::openprinttag::instance_uuid_text(
        *plan_->current.material.instance_uuid);
  if (uuid_.empty()) {
    std::array<std::uint8_t, 16> bytes{};
    random_(bytes.data(), bytes.size());
    bytes[6] = (bytes[6] & 15) | 0x40;
    bytes[8] = (bytes[8] & 63) | 0x80;
    uuid_ = nfc::openprinttag::instance_uuid_text(bytes);
  }
  auto uuid = nfc::openprinttag::parse_instance_uuid(uuid_);
  if (!uuid.ok())
    return Result::failure(uuid.error());
  auto unique = unique_identity();
  if (!unique.ok())
    return unique;
  unique = prepare_uid_owner();
  if (!unique.ok())
    return unique;
  const bool mutable_only = c["mode"].as<std::string>() == "update";
  auto mapped = nfc::openprinttag::map_spoolman(
      spool.value().as<JsonObjectConst>(), uuid.value(), *plan_, mutable_only);
  if (!mapped.ok())
    return mapped;
  auto planned = writer_.plan(*plan_);
  if (!planned.ok())
    return planned;
  view_["spool"].set(spool.value());
  view_["uid"] = plan_->uid.hex();
  view_["generation"] = std::to_string(plan_->generation);
  view_["spool_id"] = spool_id_;
  view_["previous_spool_id"] = plan_->previous_spool_id;
  view_["instance_uuid"] = uuid_;
  view_["block_size"] = 4;
  view_["block_count"] = 80;
  view_["usable_bytes"] = 312;
  view_["preserved_blocks"] = "78-79";
  view_["tag_type"] = "NXP ICODE SLIX2";
  view_["current_checksum"] =
      nfc::nfcv::format_diagnostic_checksum(plan_->current_checksum).data();
  view_["target_checksum"] =
      nfc::nfcv::format_diagnostic_checksum(plan_->target_checksum).data();
  view_["mode"] = mutable_only   ? "update"
                  : plan_->blank ? "initialize"
                                 : "rewrite";
  view_["recovering_interrupted_write"] = plan_->recovery;
  view_["repurpose"] = plan_->previous_spool_id > 0 ||
                       (plan_->current.material.instance_uuid.has_value() &&
                        plan_->current.material.instance_uuid != uuid.value());
  auto current = view_["current"].to<JsonObject>();
  nfc::openprinttag::material_json(current, plan_->current.material);
  auto blocks = view_["changed_blocks"].to<JsonArray>();
  for (std::size_t i = 0; i < plan_->count; ++i)
    blocks.add(plan_->blocks[i]);
  auto warnings = view_["warnings"].to<JsonArray>();
  if (plan_->previous_spool_id > 0)
    warnings.add("Physical NFC UID association will move from the previous "
                 "spool to this target spool after verification. The previous "
                 "spool UUID is retained.");
  warnings.add("Writing is not atomic. Keep this tag on the reader and power "
               "connected until complete.");
  warnings.add("Names exceeding OpenPrintTag limits and metadata without an "
               "exact supported mapping are omitted, never truncated. Review "
               "proposed values.");
  auto proposed = view_["proposed"].to<JsonObject>();
  const auto &m = plan_->proposed.material;
  nfc::openprinttag::material_json(proposed, m);
  if (!mutable_only && !plan_->blank)
    warnings.add("Full rewrite replaces tag-only metadata with canonical "
                 "Spoolman fields; inspect Current versus Proposed. Mutable "
                 "update preserves other fields.");
  for (const auto &warning : m.validation.warnings)
    warnings.add(warning);
  if (view_.overflowed() || measureJson(view_) > 24000)
    return fail("Preview workspace unavailable; no write authorized");
  publish("preview", "Review exact target and confirm this tag", 0,
          plan_->count);
  return Result::success();
}
Result TagWriterService::associate() {
  if (!plan_ || !plan_->verified)
    return fail("Physical verification has not passed");
  if (settings_url_ != spoolman_.settings_.url ||
      identity_key_ != spoolman_.settings_.identity_field ||
      uid_key_ != spoolman_.settings_.nfc_uid_field)
    return fail("Spoolman settings changed; restore settings before retrying "
                "association");
  publish("associating", "Physical tag verified; associating Spoolman",
          plan_->completed, plan_->count);
  auto unique = unique_identity();
  if (!unique.ok())
    return unique;
  unique = clear_previous_uid();
  if (!unique.ok())
    return unique;
  auto before = api("GET", "/spool/" + std::to_string(spool_id_));
  if (!before.ok())
    return Result::failure(before.error());
  if (before.value()["id"].as<int>() != spool_id_)
    return fail("Canonical spool ID mismatch; association pending");
  const auto identity = scalar(before.value()["extra"][identity_key_]);
  if (!identity.empty() && identity != uuid_)
    return fail(
        "Spool identity changed during writing; association requires review");
  network::BackendDocument patch;
  patch["extra"][identity_key_] = quote(uuid_);
  patch["extra"][uid_key_] = quote(plan_->uid.hex());
  if (patch.overflowed() || scalar(patch["extra"][identity_key_]) != uuid_ ||
      scalar(patch["extra"][uid_key_]) != plan_->uid.hex())
    return fail("Association workspace unavailable; retry association");
  auto updated = api("PATCH", "/spool/" + std::to_string(spool_id_),
                     patch.as<JsonVariantConst>());
  if (!updated.ok())
    return Result::failure(updated.error());
  auto after = api("GET", "/spool/" + std::to_string(spool_id_));
  if (!after.ok())
    return Result::failure(after.error());
  if (after.value()["id"].as<int>() != spool_id_ ||
      scalar(after.value()["extra"][identity_key_]) != uuid_ ||
      scalar(after.value()["extra"][uid_key_]) != plan_->uid.hex())
    return fail("Spoolman association readback mismatch");
  unique = unique_identity();
  if (!unique.ok())
    return unique;
  auto owner = uid_owner();
  if (!owner.ok())
    return Result::failure(owner.error());
  if (owner.value() != spool_id_)
    return fail("Final NFC UID ownership is not uniquely the target spool");
  association_pending_ = false;
  if (journal_)
    journal_->clear();
  publish("complete",
          "Tag and Spoolman association verified; ready to weigh and assign",
          plan_->completed, plan_->count);
  return Result::success();
}
__attribute__((noinline)) Result
TagWriterService::edit_and_report(JsonObjectConst c, bool filament) {
  auto result = edit_record(c, filament);
  if (!result.ok())
    publish("failed", result.error().message.c_str());
  return result;
}
Result TagWriterService::process(JsonObjectConst c) {
  const std::string action = c["action"] | "";
  if (association_pending_ && action != "retry_association") {
    publish("association_pending",
            "Tag written successfully; Spoolman association pending.");
    return fail("Retry pending association first");
  }
  // Editing has a separate result frame; it is not an ancestor of NFC decode.
  if (action == "update_spool" || action == "update_filament")
    return edit_and_report(c, action == "update_filament");
  Result result = fail("Unknown writer operation");
  if (action == "catalog")
    result = catalog(c);
  else if (action == "import_preview")
    result = import_preview(c);
  else if (action == "import")
    result = import_commit(c);
  else if (action == "create_spool")
    result = create_spool(c);
  else if (action == "preview")
    result = prepare(c);
  else if (action == "write") {
    if (!plan_ || view_["phase"].as<std::string>() != "preview" ||
        c["uid"].as<std::string>() != plan_->uid.hex() ||
        c["generation"].as<std::string>() !=
            std::to_string(plan_->generation) ||
        c["spool_id"].as<int>() != spool_id_ ||
        !c["previous_spool_id"].is<int>() ||
        c["previous_spool_id"].as<int>() != plan_->previous_spool_id ||
        c["target_checksum"].as<std::string>() !=
            nfc::nfcv::format_diagnostic_checksum(plan_->target_checksum)
                .data())
      result = fail("Specific confirmation does not match the current preview");
    else if (settings_url_ != spoolman_.settings_.url ||
             identity_key_ != spoolman_.settings_.identity_field ||
             uid_key_ != spoolman_.settings_.nfc_uid_field)
      result = fail("Spoolman settings changed; preview again before writing");
    else {
      if (journal_ && !journal_->save(*plan_, spool_id_,
                                      backend_identity(spoolman_.settings_))) {
        publish("failed",
                "Recovery journal could not be verified; no tag bytes written");
        return fail("Writer recovery journal unavailable");
      }
      result = writer_.execute(
          *plan_, [&](const char *phase, std::size_t done, std::size_t total) {
            publish(phase, "Keep tag present and power connected", done, total);
          });
      if (result.ok()) {
        association_pending_ = true;
        result = associate();
      }
    }
  } else if (action == "retry_association")
    result =
        association_pending_ ? associate() : fail("No association pending");
  if (!result.ok()) {
    if (association_pending_)
      publish("association_pending",
              "Tag written successfully; Spoolman association pending.",
              plan_->completed, plan_->count);
    else
      publish("failed", result.error().message.c_str(),
              plan_ ? plan_->completed : 0, plan_ ? plan_->count : 0);
  }
  return result;
}
} // namespace opentag::services
