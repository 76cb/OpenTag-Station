#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_http_server.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "boards/wt32_sc01_plus_rev_a.hpp"
#include "diagnostics/build_info.hpp"
#include "diagnostics/shared_i2c_diagnostic.hpp"
#include "hardware/display/wt32_display.hpp"
#include "hardware/scale/nau7802_device.hpp"

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
constexpr std::uint16_t wire_timeout_ms = 20U;
constexpr std::uint32_t coexistence_duration_ms = 30000U;
constexpr std::uint32_t nfc_probe_interval_ms = 5000U;
constexpr std::uint32_t scale_check_interval_ms = 20U;
constexpr std::uint32_t minimum_scale_samples = 30U;
constexpr const char* access_point_ssid = "OpenTag-I2C-Test";
constexpr const char* access_point_url = "http://192.168.4.1";

constexpr std::uint8_t st25r3916b_operation_control_register = 0x02U;
constexpr std::uint8_t st25r3916b_main_irq_register = 0x1AU;
constexpr std::uint8_t st25r3916b_last_irq_register = 0x1DU;
constexpr std::uint8_t st25r3916b_enable_oscillator = 0x80U;
constexpr std::uint8_t st25r3916b_irq_oscillator_stable = 0x80U;

constexpr char diagnostic_page[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>OpenTag Shared I2C Test</title>
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
  <h1>Shared I2C / NFC Test</h1>
  <p class="note">GPIO10 SDA · GPIO11 SCL · 100 kHz. This page polls every 5 seconds only while the bounded test is running.</p>
  <dl id="results"><dt>Status</dt><dd class="pending">Loading…</dd></dl>
  <button id="refresh" type="button">Refresh now</button>
  <script>
    const labels={clock_hz:'Clock (Hz)',sda_idle:'SDA idle',scl_idle:'SCL idle',
      nau_probe_result:'NAU7802 0x2A',nfc_probe_result:'NFC 0x50',
      nfc_identity_result:'NFC chip ID',nfc_irq_result:'NFC IRQ',
      nau_communication_result:'NAU communication',bus_recovery_attempted:'Bus recovery attempted',
      bus_recovery_success:'Bus recovery success',scan_addresses:'Full scan',
      invalid_scan_detected:'Invalid scan detected',bus_error_count:'Bus errors',
      scale_after_test:'Scale after test',nfc_after_scale:'NFC after scale',
      scale_sample_count:'Scale samples',last_raw:'Last raw reading',phase:'Phase',failure_stage:'First failing stage'};
    const order=Object.keys(labels);let timer;
    function text(v){return Array.isArray(v)?(v.length?v.join(' '):'none'):String(v)}
    function cls(v){v=String(v);return v==='PASS'||v==='ACK'||v==='HIGH'||v==='true'?'pass':
      v==='FAIL'||v==='BUS ERROR'||v==='LOW'||v==='true-invalid'?'fail':'pending'}
    async function load(){clearTimeout(timer);try{
      const r=await fetch('/api/v1/i2c-diagnostic',{cache:'no-store'});if(!r.ok)throw Error(r.status);
      const d=await r.json(),root=document.getElementById('results');root.replaceChildren();
      for(const k of order){const dt=document.createElement('dt'),dd=document.createElement('dd');
        dt.textContent=labels[k];let value=d[k];if(k==='scan_addresses')value=value.map(x=>'0x'+x.toString(16).padStart(2,'0').toUpperCase());
        dd.textContent=text(value);dd.className=cls(k==='invalid_scan_detected'&&value?'true-invalid':value);root.append(dt,dd)}
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

class SharedI2cFirmware {
 public:
  SharedI2cFirmware()
      : scale_adc_(
            Wire,
            I2cPins{Board::scale_sda, Board::scale_scl},
            Nau7802Config{
                diagnostic_clock_hz,
                Nau7802SampleRate::sps_10,
                7U,
                5U}) {}

  void setup() {
    Serial.begin(115200);
    delay(100U);
    Serial.printf(
        "OpenTag shared I2C diagnostic %s (%s)\n",
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

    if (live_.sda_idle == LineState::low || live_.scl_idle == LineState::low) {
      set_phase(Phase::bus_recovery);
      recover_bus_once();
    }

    (void)Wire.end();
    wire_ready_ = Wire.begin(Board::scale_sda, Board::scale_scl, diagnostic_clock_hz);
    Wire.setTimeOut(wire_timeout_ms);
    Wire.setClock(diagnostic_clock_hz);
    if (!wire_ready_) record_failure(FailureStage::wire_begin);

    set_phase(Phase::targeted_probes);
    live_.nau_probe_result = probe_target(nau7802_address);
    live_.nfc_probe_result = probe_target(st25r3916b_address);
    if (live_.nau_probe_result != ProbeResult::ack) {
      record_failure(FailureStage::nau_probe);
    }
    if (live_.nfc_probe_result != ProbeResult::ack) {
      record_failure(FailureStage::nfc_probe);
    }

    set_phase(Phase::full_scan);
    scan_bus();

    set_phase(Phase::nfc_register_test);
    test_nfc_registers_and_irq();

    set_phase(Phase::nau_test);
    test_nau7802();

    const bool healthy_for_coexistence =
        live_.nau_probe_result == ProbeResult::ack &&
        live_.nfc_probe_result == ProbeResult::ack &&
        live_.nfc_identity_result == CheckResult::pass &&
        live_.nau_communication_result == CheckResult::pass &&
        !live_.invalid_scan_detected;
    if (!healthy_for_coexistence) {
      live_.scale_after_test = CheckResult::skipped;
      live_.nfc_after_scale = CheckResult::skipped;
      set_phase(Phase::complete);
      return;
    }

    coexistence_started_ms_ = millis();
    last_scale_check_ms_ = coexistence_started_ms_;
    last_nfc_probe_ms_ = coexistence_started_ms_;
    set_phase(Phase::coexistence);
  }

  void sample_idle_lines() {
    (void)Wire.end();
    pinMode(Board::scale_sda, INPUT_PULLUP);
    pinMode(Board::scale_scl, INPUT_PULLUP);
    delay(3U);
    live_.sda_idle = digitalRead(Board::scale_sda) == HIGH
        ? LineState::high
        : LineState::low;
    live_.scl_idle = digitalRead(Board::scale_scl) == HIGH
        ? LineState::high
        : LineState::low;
    if (live_.sda_idle == LineState::low) record_failure(FailureStage::sda_stuck_low);
    if (live_.scl_idle == LineState::low) record_failure(FailureStage::scl_stuck_low);
    Serial.printf(
        "SDA idle=%s SCL idle=%s\n",
        to_string(live_.sda_idle),
        to_string(live_.scl_idle));
  }

  static bool wait_for_line(std::uint8_t pin, int level, std::uint32_t timeout_us) {
    const auto started_us = micros();
    while (digitalRead(pin) != level) {
      if (static_cast<std::uint32_t>(micros() - started_us) >= timeout_us) return false;
      delayMicroseconds(1U);
    }
    return true;
  }

  void recover_bus_once() {
    live_.bus_recovery_attempted = true;
    pinMode(Board::scale_sda, INPUT_PULLUP);
    pinMode(Board::scale_scl, OUTPUT_OPEN_DRAIN);
    digitalWrite(Board::scale_scl, HIGH);
    (void)wait_for_line(Board::scale_scl, HIGH, 100U);

    for (std::uint8_t pulse = 0U;
         pulse < 9U && digitalRead(Board::scale_sda) == LOW;
         ++pulse) {
      digitalWrite(Board::scale_scl, LOW);
      delayMicroseconds(5U);
      digitalWrite(Board::scale_scl, HIGH);
      (void)wait_for_line(Board::scale_scl, HIGH, 100U);
      delayMicroseconds(5U);
    }

    // Generate one STOP: SDA low while SCL low, release SCL, then release SDA.
    pinMode(Board::scale_sda, OUTPUT_OPEN_DRAIN);
    digitalWrite(Board::scale_sda, LOW);
    digitalWrite(Board::scale_scl, LOW);
    delayMicroseconds(5U);
    digitalWrite(Board::scale_scl, HIGH);
    (void)wait_for_line(Board::scale_scl, HIGH, 100U);
    delayMicroseconds(5U);
    digitalWrite(Board::scale_sda, HIGH);
    delayMicroseconds(5U);
    pinMode(Board::scale_sda, INPUT_PULLUP);
    pinMode(Board::scale_scl, INPUT_PULLUP);
    delay(2U);

    live_.bus_recovery_success =
        digitalRead(Board::scale_sda) == HIGH &&
        digitalRead(Board::scale_scl) == HIGH;
    if (!live_.bus_recovery_success) record_failure(FailureStage::bus_recovery);
    Serial.printf(
        "bus_recovery_attempted=true bus_recovery_success=%s\n",
        live_.bus_recovery_success ? "true" : "false");
  }

  ProbeResult probe_target(std::uint8_t address) {
    if (!wire_ready_) {
      ++live_.bus_error_count;
      return ProbeResult::bus_error;
    }
    Wire.beginTransmission(address);
    const auto result = classify_wire_status(Wire.endTransmission(true));
    if (result == ProbeResult::bus_error) ++live_.bus_error_count;
    return result;
  }

  void scan_bus() {
    live_.scan = {};
    if (!wire_ready_) {
      live_.invalid_scan_detected = false;
      return;
    }
    for (std::uint16_t address = first_legal_address;
         address <= last_legal_address;
         ++address) {
      Wire.beginTransmission(static_cast<std::uint8_t>(address));
      const auto result = classify_wire_status(Wire.endTransmission(true));
      if (result == ProbeResult::ack) {
        live_.scan.record(static_cast<std::uint8_t>(address));
      } else if (result == ProbeResult::bus_error) {
        ++live_.bus_error_count;
      }
    }
    live_.invalid_scan_detected = scan_is_implausible(live_.scan);
    if (live_.invalid_scan_detected) record_failure(FailureStage::invalid_scan);
    Serial.printf(
        "FULL SCAN count=%u invalid=%s\n",
        static_cast<unsigned>(live_.scan.device_count),
        live_.invalid_scan_detected ? "true" : "false");
  }

  bool note_nfc_status(std::uint8_t wire_status) {
    const auto result = classify_wire_status(wire_status);
    if (result == ProbeResult::bus_error) ++live_.bus_error_count;
    return result == ProbeResult::ack;
  }

  bool nfc_direct_command(std::uint8_t command) {
    Wire.beginTransmission(st25r3916b_address);
    Wire.write(command);
    return note_nfc_status(Wire.endTransmission(true));
  }

  bool nfc_write_register(std::uint8_t register_address, std::uint8_t value) {
    Wire.beginTransmission(st25r3916b_address);
    Wire.write(static_cast<std::uint8_t>(register_address & 0x3FU));
    Wire.write(value);
    return note_nfc_status(Wire.endTransmission(true));
  }

  bool nfc_read_register(std::uint8_t register_address, std::uint8_t& value) {
    Wire.beginTransmission(st25r3916b_address);
    Wire.write(st25r3916b_read_mode(register_address));
    if (!note_nfc_status(Wire.endTransmission(false))) return false;
    if (Wire.requestFrom(
            static_cast<std::uint16_t>(st25r3916b_address),
            static_cast<std::size_t>(1U),
            true) != 1U) {
      ++live_.bus_error_count;
      return false;
    }
    value = static_cast<std::uint8_t>(Wire.read());
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
      ++live_.bus_error_count;
      live_.nau_communication_result = CheckResult::fail;
      record_failure(FailureStage::nau_initialize);
      Serial.printf("NAU initialization failed: %s\n", initialized.error().message.c_str());
      return;
    }
    const auto calibrated = scale_adc_.internal_calibrate(1500U);
    if (!calibrated.ok()) {
      Wire.setTimeOut(wire_timeout_ms);
      ++live_.bus_error_count;
      live_.nau_communication_result = CheckResult::fail;
      record_failure(FailureStage::nau_initialize);
      Serial.printf("NAU calibration failed: %s\n", calibrated.error().message.c_str());
      return;
    }
    Wire.setTimeOut(wire_timeout_ms);
    live_.nau_communication_result = CheckResult::pass;
  }

  void poll_coexistence(std::uint32_t now_ms) {
    live_.coexistence_elapsed_ms = std::min<std::uint32_t>(
        static_cast<std::uint32_t>(now_ms - coexistence_started_ms_),
        coexistence_duration_ms);

    if (elapsed(now_ms, last_scale_check_ms_, scale_check_interval_ms)) {
      last_scale_check_ms_ = now_ms;
      const auto ready = scale_adc_.sample_ready();
      if (!ready.ok()) {
        ++live_.bus_error_count;
        scale_runtime_failed_ = true;
        record_failure(FailureStage::scale_sample);
      } else if (ready.value()) {
        const auto raw = scale_adc_.read_raw();
        if (!raw.ok()) {
          ++live_.bus_error_count;
          scale_runtime_failed_ = true;
          record_failure(FailureStage::scale_sample);
        } else {
          live_.last_raw = raw.value();
          ++live_.scale_sample_count;
        }
      }
    }

    if (elapsed(now_ms, last_nfc_probe_ms_, nfc_probe_interval_ms)) {
      last_nfc_probe_ms_ = now_ms;
      if (probe_target(st25r3916b_address) != ProbeResult::ack) {
        nfc_runtime_failed_ = true;
        record_failure(FailureStage::nfc_coexistence_probe);
      }
    }

    if (!elapsed(now_ms, coexistence_started_ms_, coexistence_duration_ms)) return;

    const auto final_nfc_probe = probe_target(st25r3916b_address);
    if (final_nfc_probe != ProbeResult::ack) {
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
    screen_.setTextSize(2U);
    screen_.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    screen_.setCursor(8, y);
    screen_.print(label);
    screen_.setTextColor(color, TFT_BLACK);
    screen_.setCursor(276, y);
    screen_.print(value);
  }

  void render_screen(const Snapshot& snapshot) {
    screen_.fillScreen(TFT_BLACK);
    screen_.setTextSize(2U);
    screen_.setTextColor(TFT_CYAN, TFT_BLACK);
    screen_.setCursor(8, 4);
    screen_.print("SHARED I2C / NFC TEST");

    draw_result_line(28, "SDA IDLE", to_string(snapshot.sda_idle),
                     snapshot.sda_idle == LineState::high ? TFT_GREEN : TFT_RED);
    draw_result_line(47, "SCL IDLE", to_string(snapshot.scl_idle),
                     snapshot.scl_idle == LineState::high ? TFT_GREEN : TFT_RED);
    draw_result_line(66, "NAU7802 0x2A", to_string(snapshot.nau_probe_result),
                     result_color(snapshot.nau_probe_result));
    draw_result_line(85, "NFC 0x50", to_string(snapshot.nfc_probe_result),
                     result_color(snapshot.nfc_probe_result));
    draw_result_line(104, "NFC CHIP ID", to_string(snapshot.nfc_identity_result),
                     result_color(snapshot.nfc_identity_result));
    draw_result_line(123, "NFC IRQ", to_string(snapshot.nfc_irq_result),
                     result_color(snapshot.nfc_irq_result));
    draw_result_line(142, "NAU COMM", to_string(snapshot.nau_communication_result),
                     result_color(snapshot.nau_communication_result));

    const char* recovery = !snapshot.bus_recovery_attempted
        ? "NOT NEEDED"
        : (snapshot.bus_recovery_success ? "PASS" : "FAIL");
    draw_result_line(161, "BUS RECOVERY", recovery,
                     !snapshot.bus_recovery_attempted || snapshot.bus_recovery_success
                         ? TFT_GREEN
                         : TFT_RED);
    draw_result_line(180, "SCALE AFTER TEST", to_string(snapshot.scale_after_test),
                     result_color(snapshot.scale_after_test));
    draw_result_line(199, "NFC AFTER SCALE", to_string(snapshot.nfc_after_scale),
                     result_color(snapshot.nfc_after_scale));

    char counters[48]{};
    std::snprintf(
        counters,
        sizeof(counters),
        "%lu   SAMPLES %lu",
        static_cast<unsigned long>(snapshot.bus_error_count),
        static_cast<unsigned long>(snapshot.scale_sample_count));
    draw_result_line(218, "BUS ERRORS", counters,
                     snapshot.bus_error_count == 0U ? TFT_GREEN : TFT_RED);

    screen_.setTextSize(1U);
    screen_.setTextColor(snapshot.invalid_scan_detected ? TFT_RED : TFT_LIGHTGREY, TFT_BLACK);
    screen_.setCursor(8, 241);
    screen_.print(snapshot.invalid_scan_detected ? "FULL SCAN: BUS CONTENTION / INVALID SCAN" : "FULL SCAN:");
    if (!snapshot.invalid_scan_detected) {
      for (std::uint8_t index = 0U; index < snapshot.scan.reported_count; ++index) {
        screen_.printf(" 0x%02X", static_cast<unsigned>(snapshot.scan.addresses[index]));
      }
      if (snapshot.scan.reported_count == 0U) screen_.print(" none");
    }

    screen_.setCursor(8, 255);
    screen_.setTextColor(TFT_YELLOW, TFT_BLACK);
    if (snapshot.phase == Phase::coexistence) {
      screen_.printf(
          "PHASE: %s (%lus / 30s)",
          to_string(snapshot.phase),
          static_cast<unsigned long>(snapshot.coexistence_elapsed_ms / 1000U));
    } else {
      screen_.printf("PHASE: %s", to_string(snapshot.phase));
    }
    screen_.setCursor(8, 269);
    screen_.setTextColor(
        snapshot.failure_stage == FailureStage::none ? TFT_GREEN : TFT_RED,
        TFT_BLACK);
    screen_.printf("FIRST FAILURE: %s", to_string(snapshot.failure_stage));
    screen_.setCursor(8, 288);
    screen_.setTextColor(TFT_CYAN, TFT_BLACK);
    screen_.printf("Wi-Fi: %s   %s", access_point_ssid, access_point_url);
    screen_.setCursor(8, 302);
    screen_.setTextColor(TFT_DARKGREY, TFT_BLACK);
    screen_.printf("Build %s", OPENTAG_GIT_SHA);
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
    auto* firmware = static_cast<SharedI2cFirmware*>(request->user_ctx);
    const auto snapshot = firmware->copy_snapshot();
    std::array<char, 2048U> json{};
    std::size_t used = 0U;
    append_json(
        json.data(), json.size(), used,
        "{\"clock_hz\":%lu,\"sda_idle\":\"%s\",\"scl_idle\":\"%s\","
        "\"nau_probe_result\":\"%s\",\"nfc_probe_result\":\"%s\","
        "\"nfc_identity_result\":\"%s\",\"nfc_irq_result\":\"%s\","
        "\"nau_communication_result\":\"%s\","
        "\"bus_recovery_attempted\":%s,\"bus_recovery_success\":%s,"
        "\"scan_addresses\":[",
        static_cast<unsigned long>(snapshot.clock_hz),
        to_string(snapshot.sda_idle),
        to_string(snapshot.scl_idle),
        to_string(snapshot.nau_probe_result),
        to_string(snapshot.nfc_probe_result),
        to_string(snapshot.nfc_identity_result),
        to_string(snapshot.nfc_irq_result),
        to_string(snapshot.nau_communication_result),
        snapshot.bus_recovery_attempted ? "true" : "false",
        snapshot.bus_recovery_success ? "true" : "false");
    for (std::uint8_t index = 0U; index < snapshot.scan.reported_count; ++index) {
      append_json(
          json.data(), json.size(), used,
          "%s%u",
          index == 0U ? "" : ",",
          static_cast<unsigned>(snapshot.scan.addresses[index]));
    }
    append_json(
        json.data(), json.size(), used,
        "],\"scan_device_count\":%u,\"scan_truncated\":%s,"
        "\"invalid_scan_detected\":%s,\"bus_error_count\":%lu,"
        "\"scale_after_test\":\"%s\",\"nfc_after_scale\":\"%s\","
        "\"scale_sample_count\":%lu,\"last_raw\":%ld,"
        "\"nfc_identity_raw\":%u,\"nfc_product\":%u,\"nfc_revision\":%u,"
        "\"phase\":\"%s\",\"failure_stage\":\"%s\",\"complete\":%s}",
        static_cast<unsigned>(snapshot.scan.device_count),
        snapshot.scan.truncated ? "true" : "false",
        snapshot.invalid_scan_detected ? "true" : "false",
        static_cast<unsigned long>(snapshot.bus_error_count),
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

  Wt32DisplayDevice screen_;
  Nau7802Device scale_adc_;
  Snapshot live_;
  mutable Snapshot published_;
  mutable portMUX_TYPE snapshot_mux_ = portMUX_INITIALIZER_UNLOCKED;
  httpd_handle_t server_{nullptr};
  bool screen_ready_{false};
  bool web_ready_{false};
  bool wire_ready_{false};
  bool scale_runtime_failed_{false};
  bool nfc_runtime_failed_{false};
  std::uint32_t coexistence_started_ms_{0U};
  std::uint32_t last_scale_check_ms_{0U};
  std::uint32_t last_nfc_probe_ms_{0U};
  std::uint32_t last_render_ms_{0U};
};

SharedI2cFirmware firmware;

}  // namespace
}  // namespace opentag::diagnostics::shared_i2c

void setup() {
  opentag::diagnostics::shared_i2c::firmware.setup();
}

void loop() {
  opentag::diagnostics::shared_i2c::firmware.loop();
}
