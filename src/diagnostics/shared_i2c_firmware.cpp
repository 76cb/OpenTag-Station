#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_http_server.h>

#include <rfal_nfc.h>
#include <rfal_rfst25r3916.h>
#include <st_errno.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>

#include "boards/wt32_sc01_plus_rev_a.hpp"
#include "diagnostics/build_info.hpp"
#include "diagnostics/shared_i2c_diagnostic.hpp"
#include "hardware/display/wt32_display.hpp"
#include "hardware/scale/nau7802_device.hpp"
#include "nfc/protocols/nfcv/tag.hpp"

namespace opentag::diagnostics::shared_i2c {
namespace {

using Board = boards::Wt32Sc01PlusRevA;
using hardware::display::Wt32DisplayDevice;
using hardware::scale::I2cPins;
using hardware::scale::Nau7802Config;
using hardware::scale::Nau7802Device;
using hardware::scale::Nau7802SampleRate;

constexpr std::uint8_t irq_pin = Board::diagnostic_nfc_interrupt;
static_assert(Board::nau7802_address == nau7802_address);
static_assert(Board::diagnostic_nfc_i2c_address == st25r3916b_address);
static_assert(Board::scale_sda == scale_sda_gpio && Board::scale_scl == scale_scl_gpio);
static_assert(Board::diagnostic_nfc_sda == nfc_sda_gpio && Board::diagnostic_nfc_scl == nfc_scl_gpio);
constexpr std::uint16_t wire_timeout_ms = 20U;
constexpr std::uint32_t coexistence_duration_ms = 30000U;
constexpr std::uint32_t inventory_interval_ms = 500U;
constexpr std::uint32_t scale_check_interval_ms = 20U;
constexpr std::uint32_t minimum_scale_samples = 30U;
constexpr const char* access_point_ssid = "OpenTag-I2C-Test";
constexpr const char* access_point_url = "http://192.168.4.1";

constexpr std::uint8_t st25r3916b_operation_control_register = 0x02U;
constexpr std::uint8_t st25r3916b_main_irq_register = 0x1AU;
constexpr std::uint8_t st25r3916b_last_irq_register = 0x1DU;
constexpr std::uint8_t st25r3916b_enable_oscillator = 0x80U;
constexpr std::uint8_t st25r3916b_irq_oscillator_stable = 0x80U;

class ScopedRfField {
 public:
  explicit ScopedRfField(RfalRfST25R3916Class& reader) : reader_(reader) {}
  ScopedRfField(const ScopedRfField&) = delete;
  ScopedRfField& operator=(const ScopedRfField&) = delete;

  [[nodiscard]] ReturnCode enable() {
    const auto result = reader_.rfalFieldOnAndStartGT();
    active_ = result == ERR_NONE;
    return result;
  }

  [[nodiscard]] ReturnCode close() {
    if (!active_) return ERR_NONE;
    const auto result = reader_.rfalFieldOff();
    active_ = result != ERR_NONE;
    return result;
  }

  ~ScopedRfField() {
    if (active_) (void)reader_.rfalFieldOff();
  }

 private:
  RfalRfST25R3916Class& reader_;
  bool active_{false};
};

bool is_rfal_i2c_error(ReturnCode result) {
  return (result & 0xFF00U) == ERR_I2C_GRP;
}

constexpr char diagnostic_page[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>OpenTag Dual I2C / NFC-V RF Test</title>
  <style>
    :root{font-family:system-ui,sans-serif;color:#e9f4f5;background:#10191c}
    body{margin:0;padding:1rem}main{max-width:48rem;margin:auto}
    h1{font-size:1.6rem;margin:.25rem 0}.note{color:#9fb4ba;margin:.3rem 0 1rem}
    dl{display:grid;grid-template-columns:minmax(12rem,1fr) 1fr;gap:.45rem 1rem;
       padding:1rem;border:1px solid #345057;border-radius:.75rem;background:#172327}
    dt{color:#9fb4ba}dd{margin:0;font-family:ui-monospace,monospace;overflow-wrap:anywhere}
    .pass{color:#69d591}.fail{color:#ff8d7f}.pending{color:#f4ce68}
    button{padding:.65rem 1rem;border:0;border-radius:.5rem;background:#167d82;color:white;font-weight:700}
  </style>
</head>
<body><main>
  <h1>Dual I2C / NFC-V RF Test</h1>
  <p class="note">Scale: GPIO10/GPIO11 · NFC: GPIO13/GPIO14 · IRQ GPIO12 · 100 kHz each. RF is enabled only during bounded inventory rounds.</p>
  <dl id="results"><dt>Status</dt><dd class="pending">Loading…</dd></dl>
  <button id="refresh" type="button">Refresh now</button>
  <script>
    const labels={scale_clock_hz:'Scale clock (Hz)',scale_sda_idle:'Scale SDA idle',scale_scl_idle:'Scale SCL idle',
      nau_probe_result:'NAU7802 0x2A',scale_bus_error_count:'Scale bus errors',
      nfc_clock_hz:'NFC clock (Hz)',nfc_sda_idle:'NFC SDA idle',nfc_scl_idle:'NFC SCL idle',
      nfc_probe_result:'NFC 0x50',
      nfc_identity_result:'NFC chip ID',nfc_irq_result:'NFC IRQ',
      rfal_initialize_result:'RFAL initialize',nfcv_poller_result:'NFC-V poller',
      rf_field_result:'RF field',iso15693_inventory_result:'ISO15693 inventory',
      tag_detected:'Tag detected',devices_found:'Devices found',uid:'UID',
      inventory_round_count:'Inventory rounds',matching_uid_round_count:'Matching UID rounds',
      uid_consistent:'UID consistent',tag_removal_seen:'Removal seen',tag_reinsertion_seen:'Reinsertion seen',
      nfc_bus_error_count:'NFC bus errors',nau_communication_result:'NAU communication',
      scale_after_test:'Scale after test',nfc_after_scale:'NFC after scale',
      scale_sample_count:'Scale samples',last_raw:'Last raw reading',phase:'Phase',failure_stage:'First failing stage'};
    const order=Object.keys(labels);let timer;
    function text(v){return Array.isArray(v)?(v.length?v.join(' '):'none'):String(v)}
    function cls(k,v){v=String(v);return v==='PASS'||v==='ACK'||v==='HIGH'||v==='true'||
      (k==='tag_detected'&&(v==='YES'||v==='NO'))?'pass':
      v==='FAIL'||v==='BUS ERROR'||v==='LOW'||v==='true-invalid'?'fail':'pending'}
    async function load(){clearTimeout(timer);try{
      const r=await fetch('/api/v1/i2c-diagnostic',{cache:'no-store'});if(!r.ok)throw Error(r.status);
      const d=await r.json(),root=document.getElementById('results');root.replaceChildren();
      for(const k of order){const dt=document.createElement('dt'),dd=document.createElement('dd');
        dt.textContent=labels[k];const value=d[k];
        dd.textContent=text(value);dd.className=cls(k,value);root.append(dt,dd)}
      if(!d.complete)timer=setTimeout(load,5000);
    }catch(e){document.getElementById('results').textContent='Diagnostic endpoint unavailable: '+e;timer=setTimeout(load,5000)}}
    document.getElementById('refresh').addEventListener('click',load);load();
  </script>
</main></body></html>)HTML";

bool elapsed(std::uint32_t now, std::uint32_t then, std::uint32_t interval) {
  return static_cast<std::uint32_t>(now - then) >= interval;
}

std::uint16_t result_color(CheckResult result) {
  switch (result) {
    case CheckResult::pass: return TFT_GREEN;
    case CheckResult::fail: return TFT_RED;
    case CheckResult::skipped: return TFT_DARKGREY;
    case CheckResult::pending: return TFT_YELLOW;
  }
  return TFT_WHITE;
}

std::uint16_t result_color(ProbeResult result) {
  switch (result) {
    case ProbeResult::ack: return TFT_GREEN;
    case ProbeResult::nack:
    case ProbeResult::bus_error: return TFT_RED;
    case ProbeResult::pending: return TFT_YELLOW;
  }
  return TFT_WHITE;
}

class DualI2cFirmware {
 public:
  DualI2cFirmware()
      : scale_adc_(
            Wire,
            I2cPins{Board::scale_sda, Board::scale_scl},
            Nau7802Config{
                scale_clock_hz,
                Nau7802SampleRate::sps_10,
                7U,
                5U}),
        reader_(&Wire1, Board::diagnostic_nfc_interrupt),
        nfc_(&reader_) {}

  void setup() {
    Serial.begin(115200);
    delay(100U);
    Serial.printf(
        "OpenTag dual I2C diagnostic %s (%s)\n",
        OPENTAG_PROJECT_VERSION,
        OPENTAG_GIT_SHA);

    initialize_screen();
    publish_and_render(true);
    start_web_server();
    characterize_bus();
  }

  void loop() {
    const auto now_ms = millis();
    if (live_.phase == Phase::coexistence) poll_coexistence(now_ms);
    if (elapsed(now_ms, last_render_ms_, 250U)) publish_and_render(false);
    delay(2U);
  }

 private:
  void initialize_screen() {
    screen_ready_ = screen_.init();
    if (!screen_ready_) {
      Serial.println("DISPLAY initialization failed; serial and browser remain available");
      return;
    }
    screen_.setRotation(Board::display_rotation);
    screen_.setColorDepth(16);
    screen_.setBrightness(220U);
    screen_.fillScreen(TFT_BLACK);
    screen_ready_ = screen_.width() == Board::display_width &&
                    screen_.height() == Board::display_height;
  }

  void set_phase(Phase phase) {
    live_.phase = phase;
    Serial.printf("DIAGNOSTIC PHASE: %s\n", to_string(phase));
    publish_and_render(true);
  }

  void record_failure(FailureStage stage) {
    if (live_.failure_stage == FailureStage::none) live_.failure_stage = stage;
  }

  void characterize_bus() {
    set_phase(Phase::line_state);
    sample_idle_lines();

    (void)Wire.end();
    scale_wire_ready_ = Wire.begin(Board::scale_sda, Board::scale_scl, scale_clock_hz);
    Wire.setTimeOut(wire_timeout_ms);
    Wire.setClock(scale_clock_hz);
    if (!scale_wire_ready_) record_failure(FailureStage::scale_wire_begin);

    (void)Wire1.end();
    nfc_wire_ready_ = Wire1.begin(Board::diagnostic_nfc_sda, Board::diagnostic_nfc_scl, nfc_clock_hz);
    Wire1.setTimeOut(wire_timeout_ms);
    Wire1.setClock(nfc_clock_hz);
    if (!nfc_wire_ready_) record_failure(FailureStage::nfc_wire_begin);

    set_phase(Phase::scale_probe);
    live_.nau_probe_result = probe_scale_target(nau7802_address);
    if (live_.nau_probe_result != ProbeResult::ack) {
      record_failure(FailureStage::nau_probe);
    }

    set_phase(Phase::nfc_probe);
    live_.nfc_probe_result = probe_nfc_target(st25r3916b_address);
    if (live_.nfc_probe_result != ProbeResult::ack) {
      record_failure(FailureStage::nfc_probe);
    }

    set_phase(Phase::nfc_register_test);
    test_nfc_registers_and_irq();

    set_phase(Phase::nau_test);
    test_nau7802();

    const bool healthy_for_coexistence =
        live_.nau_probe_result == ProbeResult::ack &&
        live_.nfc_probe_result == ProbeResult::ack &&
        live_.nfc_identity_result == CheckResult::pass &&
        live_.nfc_irq_result == CheckResult::pass &&
        live_.nau_communication_result == CheckResult::pass;
    if (!healthy_for_coexistence) {
      skip_rf_results();
      live_.scale_after_test = CheckResult::skipped;
      live_.nfc_after_scale = CheckResult::skipped;
      set_phase(Phase::complete);
      print_final_report();
      return;
    }

    if (!initialize_nfcv()) {
      live_.scale_after_test = CheckResult::skipped;
      live_.nfc_after_scale = CheckResult::fail;
      set_phase(Phase::complete);
      print_final_report();
      return;
    }

    coexistence_started_ms_ = millis();
    last_scale_check_ms_ = coexistence_started_ms_;
    last_inventory_ms_ = coexistence_started_ms_ - inventory_interval_ms;
    set_phase(Phase::coexistence);
  }

  void skip_rf_results() {
    live_.rfal_initialize_result = CheckResult::skipped;
    live_.nfcv_poller_result = CheckResult::skipped;
    live_.rf_field_result = CheckResult::skipped;
    live_.iso15693_inventory_result = CheckResult::skipped;
  }

  void note_rfal_error(ReturnCode result) {
    if (is_rfal_i2c_error(result)) ++live_.nfc_bus_error_count;
    Serial.printf("ELECHOUSE RFAL error: 0x%04X\n", static_cast<unsigned>(result));
  }

  bool initialize_nfcv() {
    set_phase(Phase::rfal_initialize);
    const auto initialized = nfc_.rfalNfcInitialize();
    if (initialized != ERR_NONE) {
      note_rfal_error(initialized);
      live_.rfal_initialize_result = CheckResult::fail;
      live_.nfcv_poller_result = CheckResult::skipped;
      live_.rf_field_result = CheckResult::skipped;
      live_.iso15693_inventory_result = CheckResult::skipped;
      record_failure(FailureStage::rfal_initialize);
      return false;
    }
    live_.rfal_initialize_result = CheckResult::pass;

    set_phase(Phase::nfcv_poller);
    const auto poller = nfc_.rfalNfcvPollerInitialize();
    if (poller != ERR_NONE) {
      note_rfal_error(poller);
      live_.nfcv_poller_result = CheckResult::fail;
      live_.rf_field_result = CheckResult::skipped;
      live_.iso15693_inventory_result = CheckResult::skipped;
      record_failure(FailureStage::nfcv_poller_initialize);
      return false;
    }
    live_.nfcv_poller_result = CheckResult::pass;
    return true;
  }

  void sample_idle_lines() {
    (void)Wire.end();
    (void)Wire1.end();
    pinMode(Board::scale_sda, INPUT_PULLUP);
    pinMode(Board::scale_scl, INPUT_PULLUP);
    pinMode(Board::diagnostic_nfc_sda, INPUT_PULLUP);
    pinMode(Board::diagnostic_nfc_scl, INPUT_PULLUP);
    delay(3U);
    live_.scale_sda_idle = digitalRead(Board::scale_sda) == HIGH
        ? LineState::high
        : LineState::low;
    live_.scale_scl_idle = digitalRead(Board::scale_scl) == HIGH
        ? LineState::high
        : LineState::low;
    live_.nfc_sda_idle = digitalRead(Board::diagnostic_nfc_sda) == HIGH
        ? LineState::high
        : LineState::low;
    live_.nfc_scl_idle = digitalRead(Board::diagnostic_nfc_scl) == HIGH
        ? LineState::high
        : LineState::low;
    if (live_.scale_sda_idle == LineState::low) record_failure(FailureStage::scale_sda_stuck_low);
    if (live_.scale_scl_idle == LineState::low) record_failure(FailureStage::scale_scl_stuck_low);
    if (live_.nfc_sda_idle == LineState::low) record_failure(FailureStage::nfc_sda_stuck_low);
    if (live_.nfc_scl_idle == LineState::low) record_failure(FailureStage::nfc_scl_stuck_low);
    Serial.printf(
        "scale SDA=%s SCL=%s; NFC SDA=%s SCL=%s\n",
        to_string(live_.scale_sda_idle),
        to_string(live_.scale_scl_idle),
        to_string(live_.nfc_sda_idle),
        to_string(live_.nfc_scl_idle));
  }

  static bool wait_for_line(std::uint8_t pin, int level, std::uint32_t timeout_us) {
    const auto started_us = micros();
    while (digitalRead(pin) != level) {
      if (static_cast<std::uint32_t>(micros() - started_us) >= timeout_us) return false;
      delayMicroseconds(1U);
    }
    return true;
  }

  ProbeResult probe_scale_target(std::uint8_t address) {
    if (!scale_wire_ready_) {
      ++live_.scale_bus_error_count;
      return ProbeResult::bus_error;
    }
    Wire.beginTransmission(address);
    const auto result = classify_wire_status(Wire.endTransmission(true));
    if (result == ProbeResult::bus_error) ++live_.scale_bus_error_count;
    return result;
  }

  ProbeResult probe_nfc_target(std::uint8_t address) {
    if (!nfc_wire_ready_) {
      ++live_.nfc_bus_error_count;
      return ProbeResult::bus_error;
    }
    Wire1.beginTransmission(address);
    const auto result = classify_wire_status(Wire1.endTransmission(true));
    if (result == ProbeResult::bus_error) ++live_.nfc_bus_error_count;
    return result;
  }

  bool note_nfc_status(std::uint8_t wire_status) {
    const auto result = classify_wire_status(wire_status);
    if (result == ProbeResult::bus_error) ++live_.nfc_bus_error_count;
    return result == ProbeResult::ack;
  }

  bool nfc_direct_command(std::uint8_t command) {
    Wire1.beginTransmission(st25r3916b_address);
    Wire1.write(command);
    return note_nfc_status(Wire1.endTransmission(true));
  }

  bool nfc_write_register(std::uint8_t register_address, std::uint8_t value) {
    Wire1.beginTransmission(st25r3916b_address);
    Wire1.write(static_cast<std::uint8_t>(register_address & 0x3FU));
    Wire1.write(value);
    return note_nfc_status(Wire1.endTransmission(true));
  }

  bool nfc_read_register(std::uint8_t register_address, std::uint8_t& value) {
    Wire1.beginTransmission(st25r3916b_address);
    Wire1.write(st25r3916b_read_mode(register_address));
    if (!note_nfc_status(Wire1.endTransmission(false))) return false;
    if (Wire1.requestFrom(
            static_cast<std::uint16_t>(st25r3916b_address),
            static_cast<std::size_t>(1U),
            true) != 1U) {
      ++live_.nfc_bus_error_count;
      return false;
    }
    value = static_cast<std::uint8_t>(Wire1.read());
    return true;
  }

  bool clear_nfc_irq_status() {
    std::uint8_t ignored = 0U;
    for (std::uint8_t address = st25r3916b_main_irq_register;
         address <= st25r3916b_last_irq_register;
         ++address) {
      if (!nfc_read_register(address, ignored)) return false;
    }
    return true;
  }

  void test_nfc_registers_and_irq() {
    if (live_.nfc_probe_result != ProbeResult::ack) {
      live_.nfc_identity_result = CheckResult::skipped;
      live_.nfc_irq_result = CheckResult::skipped;
      return;
    }
    if (!nfc_direct_command(st25r3916b_set_default_command)) {
      live_.nfc_identity_result = CheckResult::fail;
      live_.nfc_irq_result = CheckResult::skipped;
      record_failure(FailureStage::nfc_set_default);
      return;
    }
    delay(2U);

    std::uint8_t raw_identity = 0U;
    if (!nfc_read_register(st25r3916b_identity_register, raw_identity)) {
      live_.nfc_identity_result = CheckResult::fail;
      live_.nfc_irq_result = CheckResult::skipped;
      record_failure(FailureStage::nfc_identity_read);
      return;
    }
    live_.nfc_identity = decode_st25r3916b_identity(raw_identity);
    if (!live_.nfc_identity.is_st25r3916b()) {
      live_.nfc_identity_result = CheckResult::fail;
      live_.nfc_irq_result = CheckResult::skipped;
      record_failure(FailureStage::nfc_identity_mismatch);
      return;
    }
    live_.nfc_identity_result = CheckResult::pass;
    Serial.printf(
        "NFC identity raw=0x%02X product=0x%02X revision=%u\n",
        static_cast<unsigned>(live_.nfc_identity.raw),
        static_cast<unsigned>(live_.nfc_identity.product),
        static_cast<unsigned>(live_.nfc_identity.revision));

    pinMode(irq_pin, INPUT);
    if (digitalRead(irq_pin) == HIGH) {
      if (!clear_nfc_irq_status()) {
        live_.nfc_irq_result = CheckResult::fail;
        record_failure(FailureStage::nfc_irq_status);
        return;
      }
      delay(1U);
      if (digitalRead(irq_pin) == HIGH) {
        live_.nfc_irq_result = CheckResult::fail;
        record_failure(FailureStage::nfc_irq_not_clear);
        return;
      }
    }

    // Starting only the oscillator/regulator generates I_osc without enabling
    // RX, TX, an RF field, RFAL, or any tag operation.
    if (!nfc_write_register(
            st25r3916b_operation_control_register,
            st25r3916b_enable_oscillator)) {
      live_.nfc_irq_result = CheckResult::fail;
      record_failure(FailureStage::nfc_irq_start_oscillator);
      return;
    }
    if (!wait_for_line(irq_pin, HIGH, 50000U)) {
      live_.nfc_irq_result = CheckResult::fail;
      record_failure(FailureStage::nfc_irq_timeout);
      (void)nfc_direct_command(st25r3916b_set_default_command);
      return;
    }
    std::uint8_t main_irq = 0U;
    if (!nfc_read_register(st25r3916b_main_irq_register, main_irq) ||
        (main_irq & st25r3916b_irq_oscillator_stable) == 0U) {
      live_.nfc_irq_result = CheckResult::fail;
      record_failure(FailureStage::nfc_irq_status);
      (void)nfc_direct_command(st25r3916b_set_default_command);
      return;
    }
    (void)clear_nfc_irq_status();
    if (!wait_for_line(irq_pin, LOW, 10000U)) {
      live_.nfc_irq_result = CheckResult::fail;
      record_failure(FailureStage::nfc_irq_not_cleared);
      (void)nfc_direct_command(st25r3916b_set_default_command);
      return;
    }
    if (!nfc_direct_command(st25r3916b_set_default_command)) {
      live_.nfc_irq_result = CheckResult::fail;
      record_failure(FailureStage::nfc_set_default);
      return;
    }
    live_.nfc_irq_result = CheckResult::pass;
  }

  void test_nau7802() {
    if (live_.nau_probe_result != ProbeResult::ack) {
      live_.nau_communication_result = CheckResult::skipped;
      return;
    }
    const auto initialized = scale_adc_.initialize(1500U);
    if (!initialized.ok()) {
      Wire.setTimeOut(wire_timeout_ms);
      ++live_.scale_bus_error_count;
      live_.nau_communication_result = CheckResult::fail;
      record_failure(FailureStage::nau_initialize);
      Serial.printf("NAU initialization failed: %s\n", initialized.error().message.c_str());
      return;
    }
    const auto calibrated = scale_adc_.internal_calibrate(1500U);
    if (!calibrated.ok()) {
      Wire.setTimeOut(wire_timeout_ms);
      ++live_.scale_bus_error_count;
      live_.nau_communication_result = CheckResult::fail;
      record_failure(FailureStage::nau_initialize);
      Serial.printf("NAU calibration failed: %s\n", calibrated.error().message.c_str());
      return;
    }
    Wire.setTimeOut(wire_timeout_ms);
    live_.nau_communication_result = CheckResult::pass;
  }

  bool post_inventory_transport_healthy() {
    if (probe_nfc_target(st25r3916b_address) != ProbeResult::ack) {
      record_failure(FailureStage::nfc_post_inventory_probe);
      return false;
    }

    std::uint8_t raw_identity = 0U;
    if (!nfc_read_register(st25r3916b_identity_register, raw_identity)) {
      record_failure(FailureStage::nfc_post_inventory_identity);
      return false;
    }
    const auto identity = decode_st25r3916b_identity(raw_identity);
    if (!identity.is_st25r3916b()) {
      record_failure(FailureStage::nfc_post_inventory_identity);
      return false;
    }
    return true;
  }

  bool record_inventory(
      const std::array<rfalNfcvListenDevice, RFAL_NFC_MAX_DEVICES>& devices,
      std::uint8_t device_count) {
    live_.devices_found = device_count;
    live_.uid.fill('\0');

    std::array<opentag::nfc::nfcv::Uid, RFAL_NFC_MAX_DEVICES> normalized{};
    for (std::uint8_t index = 0U; index < device_count; ++index) {
      const auto uid = opentag::nfc::nfcv::Uid::from_wire_lsb_first(
          core::ByteView(
              devices[index].InvRes.UID,
              RFAL_NFCV_UID_LEN));
      if (!uid.ok()) {
        live_.iso15693_inventory_result = CheckResult::fail;
        record_failure(FailureStage::nfcv_uid);
        Serial.printf("NFC-V UID rejected: %s\n", uid.error().message.c_str());
        return false;
      }
      normalized[index] = uid.value();
    }

    live_.iso15693_inventory_result = CheckResult::pass;
    if (device_count == 0U) {
      live_.tag_detected = TagDetected::no;
      if (last_devices_found_ > 0U) live_.tag_removal_seen = true;
    } else if (device_count == 1U) {
      live_.tag_detected = TagDetected::yes;
      live_.uid = format_diagnostic_uid(normalized[0].bytes);
      if (!reference_uid_.has_value()) {
        reference_uid_ = normalized[0];
        live_.matching_uid_round_count = 1U;
      } else if (*reference_uid_ == normalized[0]) {
        ++live_.matching_uid_round_count;
        if (live_.tag_removal_seen) live_.tag_reinsertion_seen = true;
      } else {
        live_.uid_consistent = false;
        record_failure(FailureStage::nfc_uid_changed);
        return false;
      }
    } else {
      live_.tag_detected = TagDetected::multiple;
    }
    last_devices_found_ = device_count;
    return true;
  }

  bool run_inventory_round() {
    ++live_.inventory_round_count;
    ScopedRfField field(reader_);
    const auto field_on = field.enable();
    if (field_on != ERR_NONE) {
      note_rfal_error(field_on);
      live_.rf_field_result = CheckResult::fail;
      live_.iso15693_inventory_result = CheckResult::skipped;
      record_failure(FailureStage::rf_field_on);
      return false;
    }
    live_.rf_field_result = CheckResult::pass;

    rfalNfcvInventoryRes presence{};
    const auto presence_result = nfc_.rfalNfcvPollerCheckPresence(&presence);
    const bool presence_valid =
        presence_result == ERR_NONE || presence_result == ERR_TIMEOUT;

    std::array<rfalNfcvListenDevice, RFAL_NFC_MAX_DEVICES> devices{};
    std::uint8_t device_count = 0U;
    ReturnCode inventory_result = ERR_WRONG_STATE;
    if (presence_valid) {
      inventory_result = nfc_.rfalNfcvPollerCollisionResolution(
          RFAL_COMPLIANCE_MODE_NFC,
          static_cast<std::uint8_t>(devices.size()),
          devices.data(),
          &device_count);
    }

    const auto field_off = field.close();
    if (field_off != ERR_NONE) {
      note_rfal_error(field_off);
      live_.rf_field_result = CheckResult::fail;
      record_failure(FailureStage::rf_field_off);
    }

    const bool transport_healthy = post_inventory_transport_healthy();
    if (!transport_healthy) nfc_runtime_failed_ = true;

    if (!presence_valid) {
      note_rfal_error(presence_result);
      live_.iso15693_inventory_result = CheckResult::fail;
      record_failure(FailureStage::nfcv_presence);
      return false;
    }
    if (inventory_result != ERR_NONE) {
      note_rfal_error(inventory_result);
      live_.iso15693_inventory_result = CheckResult::fail;
      record_failure(FailureStage::nfcv_inventory);
      return false;
    }
    if (field_off != ERR_NONE || !transport_healthy) {
      live_.iso15693_inventory_result = CheckResult::fail;
      return false;
    }

    return record_inventory(devices, device_count);
  }

  void poll_coexistence(std::uint32_t now_ms) {
    live_.coexistence_elapsed_ms = std::min<std::uint32_t>(
        static_cast<std::uint32_t>(now_ms - coexistence_started_ms_),
        coexistence_duration_ms);

    if (elapsed(now_ms, last_scale_check_ms_, scale_check_interval_ms)) {
      last_scale_check_ms_ = now_ms;
      const auto ready = scale_adc_.sample_ready();
      if (!ready.ok()) {
        ++live_.scale_bus_error_count;
        scale_runtime_failed_ = true;
        record_failure(FailureStage::scale_sample);
      } else if (ready.value()) {
        const auto raw = scale_adc_.read_raw();
        if (!raw.ok()) {
          ++live_.scale_bus_error_count;
          scale_runtime_failed_ = true;
          record_failure(FailureStage::scale_sample);
        } else {
          live_.last_raw = raw.value();
          ++live_.scale_sample_count;
        }
      }
    }

    if (elapsed(now_ms, last_inventory_ms_, inventory_interval_ms)) {
      last_inventory_ms_ = now_ms;
      if (!run_inventory_round()) {
        nfc_runtime_failed_ = true;
      }
    }

    if (!elapsed(now_ms, coexistence_started_ms_, coexistence_duration_ms)) return;

    if (!post_inventory_transport_healthy()) {
      nfc_runtime_failed_ = true;
      record_failure(FailureStage::nfc_coexistence_probe);
    }
    live_.scale_after_test =
        !scale_runtime_failed_ && live_.scale_sample_count >= minimum_scale_samples
        ? CheckResult::pass
        : CheckResult::fail;
    if (live_.scale_sample_count < minimum_scale_samples) {
      record_failure(FailureStage::insufficient_scale_samples);
    }
    live_.nfc_after_scale = !nfc_runtime_failed_
        ? CheckResult::pass
        : CheckResult::fail;
    set_phase(Phase::complete);
    print_final_report();
  }

  void print_final_report() const {
    Serial.printf("Scale 0x2A            %s\n", to_string(live_.nau_probe_result));
    Serial.printf("NFC 0x50              %s\n", to_string(live_.nfc_probe_result));
    Serial.printf("NFC chip ID           %s\n", to_string(live_.nfc_identity_result));
    Serial.printf("NFC IRQ               %s\n", to_string(live_.nfc_irq_result));
    Serial.printf("RFAL initialize       %s\n", to_string(live_.rfal_initialize_result));
    Serial.printf("NFC-V poller          %s\n", to_string(live_.nfcv_poller_result));
    Serial.printf("RF field              %s\n", to_string(live_.rf_field_result));
    Serial.printf("ISO15693 inventory    %s\n", to_string(live_.iso15693_inventory_result));
    Serial.printf("Tag detected          %s\n", to_string(live_.tag_detected));
    Serial.printf("Devices found         %u\n", static_cast<unsigned>(live_.devices_found));
    Serial.printf("UID                   %s\n", live_.uid[0] == '\0' ? "-" : live_.uid.data());
    Serial.printf("Inventory rounds      %lu\n", static_cast<unsigned long>(live_.inventory_round_count));
    Serial.printf("Matching UID rounds   %lu\n", static_cast<unsigned long>(live_.matching_uid_round_count));
    Serial.printf("UID consistent        %s\n", live_.uid_consistent ? "PASS" : "FAIL");
    Serial.printf("Tag removal seen      %s\n", live_.tag_removal_seen ? "YES" : "NO");
    Serial.printf("Tag reinsertion seen  %s\n", live_.tag_reinsertion_seen ? "YES" : "NO");
    Serial.printf("NFC bus errors        %lu\n", static_cast<unsigned long>(live_.nfc_bus_error_count));
    Serial.printf("Scale bus errors      %lu\n", static_cast<unsigned long>(live_.scale_bus_error_count));
    Serial.printf("Scale after test      %s\n", to_string(live_.scale_after_test));
    Serial.printf("NFC after scale       %s\n", to_string(live_.nfc_after_scale));
    Serial.printf("First failing stage   %s\n", to_string(live_.failure_stage));
  }

  void publish_snapshot() {
    portENTER_CRITICAL(&snapshot_mux_);
    published_ = live_;
    portEXIT_CRITICAL(&snapshot_mux_);
  }

  Snapshot copy_snapshot() const {
    Snapshot copy;
    portENTER_CRITICAL(&snapshot_mux_);
    copy = published_;
    portEXIT_CRITICAL(&snapshot_mux_);
    return copy;
  }

  void publish_and_render(bool force) {
    const auto now_ms = millis();
    if (!force && !elapsed(now_ms, last_render_ms_, 250U)) return;
    last_render_ms_ = now_ms;
    publish_snapshot();
    if (screen_ready_) render_screen(live_);
  }

  void draw_result_line(
      std::int32_t y,
      const char* label,
      const char* value,
      std::uint16_t color) {
    screen_.setTextSize(1U);
    screen_.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    screen_.setCursor(8, y);
    screen_.print(label);
    screen_.setTextColor(color, TFT_BLACK);
    screen_.setCursor(230, y);
    screen_.print(value);
  }

  void render_screen(const Snapshot& snapshot) {
    screen_.fillScreen(TFT_BLACK);
    screen_.setTextSize(2U);
    screen_.setTextColor(TFT_CYAN, TFT_BLACK);
    screen_.setCursor(8, 4);
    screen_.print("DUAL I2C / NFC-V RF TEST");

    draw_result_line(24, "SCALE 0x2A", to_string(snapshot.nau_probe_result),
                     result_color(snapshot.nau_probe_result));
    draw_result_line(38, "NFC 0x50", to_string(snapshot.nfc_probe_result),
                     result_color(snapshot.nfc_probe_result));
    draw_result_line(52, "NFC CHIP ID", to_string(snapshot.nfc_identity_result),
                     result_color(snapshot.nfc_identity_result));
    draw_result_line(66, "NFC IRQ", to_string(snapshot.nfc_irq_result),
                     result_color(snapshot.nfc_irq_result));
    draw_result_line(80, "RFAL INITIALIZE", to_string(snapshot.rfal_initialize_result),
                     result_color(snapshot.rfal_initialize_result));
    draw_result_line(94, "NFC-V POLLER", to_string(snapshot.nfcv_poller_result),
                     result_color(snapshot.nfcv_poller_result));
    draw_result_line(108, "RF FIELD", to_string(snapshot.rf_field_result),
                     result_color(snapshot.rf_field_result));
    draw_result_line(122, "ISO15693 INVENTORY", to_string(snapshot.iso15693_inventory_result),
                     result_color(snapshot.iso15693_inventory_result));
    draw_result_line(
        136,
        "TAG DETECTED",
        to_string(snapshot.tag_detected),
        snapshot.tag_detected == TagDetected::pending
            ? TFT_YELLOW
            : snapshot.tag_detected == TagDetected::multiple ? TFT_ORANGE : TFT_GREEN);

    char value[64]{};
    std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(snapshot.devices_found));
    draw_result_line(150, "DEVICES FOUND", value, TFT_LIGHTGREY);
    draw_result_line(164, "UID", snapshot.uid[0] == '\0' ? "-" : snapshot.uid.data(), TFT_LIGHTGREY);

    std::snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(snapshot.nfc_bus_error_count));
    draw_result_line(178, "NFC BUS ERRORS", value,
                     snapshot.nfc_bus_error_count == 0U ? TFT_GREEN : TFT_RED);
    std::snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(snapshot.scale_bus_error_count));
    draw_result_line(192, "SCALE BUS ERRORS", value,
                     snapshot.scale_bus_error_count == 0U ? TFT_GREEN : TFT_RED);
    draw_result_line(206, "SCALE AFTER TEST", to_string(snapshot.scale_after_test),
                     result_color(snapshot.scale_after_test));
    draw_result_line(220, "NFC AFTER SCALE", to_string(snapshot.nfc_after_scale),
                     result_color(snapshot.nfc_after_scale));
    std::snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(snapshot.scale_sample_count));
    draw_result_line(234, "SCALE SAMPLES", value, TFT_LIGHTGREY);
    std::snprintf(
        value, sizeof(value), "%lu / %lu",
        static_cast<unsigned long>(snapshot.matching_uid_round_count),
        static_cast<unsigned long>(snapshot.inventory_round_count));
    draw_result_line(248, "UID / INVENTORY ROUNDS", value,
                     snapshot.uid_consistent ? TFT_GREEN : TFT_RED);

    screen_.setTextSize(1U);
    screen_.setCursor(8, 266);
    screen_.setTextColor(TFT_YELLOW, TFT_BLACK);
    if (snapshot.phase == Phase::coexistence) {
      screen_.printf(
          "PHASE: %s (%lus / 30s)",
          to_string(snapshot.phase),
          static_cast<unsigned long>(snapshot.coexistence_elapsed_ms / 1000U));
    } else {
      screen_.printf("PHASE: %s", to_string(snapshot.phase));
    }
    screen_.setCursor(8, 280);
    screen_.setTextColor(
        snapshot.failure_stage == FailureStage::none ? TFT_GREEN : TFT_RED,
        TFT_BLACK);
    screen_.printf("FIRST FAILURE: %s", to_string(snapshot.failure_stage));
    screen_.setCursor(8, 294);
    screen_.setTextColor(TFT_CYAN, TFT_BLACK);
    screen_.printf("Wi-Fi: %s   %s", access_point_ssid, access_point_url);
  }

  static esp_err_t root_handler(httpd_req_t* request) {
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, diagnostic_page, HTTPD_RESP_USE_STRLEN);
  }

  static void append_json(
      char* buffer,
      std::size_t capacity,
      std::size_t& used,
      const char* format,
      ...) {
    if (used >= capacity) return;
    va_list arguments;
    va_start(arguments, format);
    const auto available = capacity - used;
    const int written = std::vsnprintf(buffer + used, available, format, arguments);
    va_end(arguments);
    if (written < 0) return;
    used += std::min<std::size_t>(static_cast<std::size_t>(written), available - 1U);
  }

  static esp_err_t api_handler(httpd_req_t* request) {
    auto* firmware = static_cast<DualI2cFirmware*>(request->user_ctx);
    const auto snapshot = firmware->copy_snapshot();
    std::array<char, 3072U> json{};
    std::size_t used = 0U;
    append_json(
        json.data(), json.size(), used,
        "{\"scale_clock_hz\":%lu,\"scale_sda_idle\":\"%s\",\"scale_scl_idle\":\"%s\","
        "\"nau_probe_result\":\"%s\",\"scale_bus_error_count\":%lu,"
        "\"nfc_clock_hz\":%lu,\"nfc_sda_idle\":\"%s\",\"nfc_scl_idle\":\"%s\","
        "\"nfc_probe_result\":\"%s\","
        "\"nfc_identity_result\":\"%s\",\"nfc_irq_result\":\"%s\","
        "\"rfal_initialize_result\":\"%s\",\"nfcv_poller_result\":\"%s\","
        "\"rf_field_result\":\"%s\",\"iso15693_inventory_result\":\"%s\","
        "\"tag_detected\":\"%s\",\"devices_found\":%u,\"uid\":\"%s\","
        "\"inventory_round_count\":%lu,\"matching_uid_round_count\":%lu,"
        "\"uid_consistent\":%s,\"tag_removal_seen\":%s,\"tag_reinsertion_seen\":%s,"
        "\"nfc_bus_error_count\":%lu,\"nau_communication_result\":\"%s\","
        "\"scale_after_test\":\"%s\",\"nfc_after_scale\":\"%s\","
        "\"scale_sample_count\":%lu,\"last_raw\":%ld,"
        "\"nfc_identity_raw\":%u,\"nfc_product\":%u,\"nfc_revision\":%u,"
        "\"phase\":\"%s\",\"failure_stage\":\"%s\",\"complete\":%s}",
        static_cast<unsigned long>(snapshot.scale_clock_hz),
        to_string(snapshot.scale_sda_idle),
        to_string(snapshot.scale_scl_idle),
        to_string(snapshot.nau_probe_result),
        static_cast<unsigned long>(snapshot.scale_bus_error_count),
        static_cast<unsigned long>(snapshot.nfc_clock_hz),
        to_string(snapshot.nfc_sda_idle),
        to_string(snapshot.nfc_scl_idle),
        to_string(snapshot.nfc_probe_result),
        to_string(snapshot.nfc_identity_result),
        to_string(snapshot.nfc_irq_result),
        to_string(snapshot.rfal_initialize_result),
        to_string(snapshot.nfcv_poller_result),
        to_string(snapshot.rf_field_result),
        to_string(snapshot.iso15693_inventory_result),
        to_string(snapshot.tag_detected),
        static_cast<unsigned>(snapshot.devices_found),
        snapshot.uid.data(),
        static_cast<unsigned long>(snapshot.inventory_round_count),
        static_cast<unsigned long>(snapshot.matching_uid_round_count),
        snapshot.uid_consistent ? "true" : "false",
        snapshot.tag_removal_seen ? "true" : "false",
        snapshot.tag_reinsertion_seen ? "true" : "false",
        static_cast<unsigned long>(snapshot.nfc_bus_error_count),
        to_string(snapshot.nau_communication_result),
        to_string(snapshot.scale_after_test),
        to_string(snapshot.nfc_after_scale),
        static_cast<unsigned long>(snapshot.scale_sample_count),
        static_cast<long>(snapshot.last_raw),
        static_cast<unsigned>(snapshot.nfc_identity.raw),
        static_cast<unsigned>(snapshot.nfc_identity.product),
        static_cast<unsigned>(snapshot.nfc_identity.revision),
        to_string(snapshot.phase),
        to_string(snapshot.failure_stage),
        snapshot.phase == Phase::complete ? "true" : "false");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, json.data(), static_cast<ssize_t>(used));
  }

  void start_web_server() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    web_ready_ = WiFi.softAP(access_point_ssid);
    if (!web_ready_) {
      Serial.println("Diagnostic Wi-Fi AP failed to start");
      return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 2U;
    config.max_open_sockets = 2U;
    config.lru_purge_enable = true;
    config.stack_size = 6144U;
    config.recv_wait_timeout = 5U;
    config.send_wait_timeout = 5U;
    if (httpd_start(&server_, &config) != ESP_OK) {
      Serial.println("Diagnostic HTTP server failed to start");
      web_ready_ = false;
      return;
    }

    httpd_uri_t root{};
    root.uri = "/";
    root.method = HTTP_GET;
    root.handler = root_handler;
    root.user_ctx = this;
    httpd_uri_t api{};
    api.uri = "/api/v1/i2c-diagnostic";
    api.method = HTTP_GET;
    api.handler = api_handler;
    api.user_ctx = this;
    if (httpd_register_uri_handler(server_, &root) != ESP_OK ||
        httpd_register_uri_handler(server_, &api) != ESP_OK) {
      Serial.println("Diagnostic HTTP routes failed to register");
      httpd_stop(server_);
      server_ = nullptr;
      web_ready_ = false;
      return;
    }
    Serial.printf("Diagnostic browser page: %s (%s)\n", access_point_url, access_point_ssid);
  }

  Wt32DisplayDevice screen_{false};
  Nau7802Device scale_adc_;
  RfalRfST25R3916Class reader_;
  RfalNfcClass nfc_;
  Snapshot live_;
  mutable Snapshot published_;
  mutable portMUX_TYPE snapshot_mux_ = portMUX_INITIALIZER_UNLOCKED;
  httpd_handle_t server_{nullptr};
  bool screen_ready_{false};
  bool web_ready_{false};
  bool scale_wire_ready_{false};
  bool nfc_wire_ready_{false};
  bool scale_runtime_failed_{false};
  bool nfc_runtime_failed_{false};
  std::uint32_t coexistence_started_ms_{0U};
  std::uint32_t last_scale_check_ms_{0U};
  std::uint32_t last_inventory_ms_{0U};
  std::uint32_t last_render_ms_{0U};
  std::uint8_t last_devices_found_{0U};
  std::optional<opentag::nfc::nfcv::Uid> reference_uid_;
};

DualI2cFirmware firmware;

}  // namespace
}  // namespace opentag::diagnostics::shared_i2c

void setup() {
  opentag::diagnostics::shared_i2c::firmware.setup();
}

void loop() {
  opentag::diagnostics::shared_i2c::firmware.loop();
}
