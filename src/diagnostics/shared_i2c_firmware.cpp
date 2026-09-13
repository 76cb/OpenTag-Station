#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_http_server.h>

#include <rfal_nfc.h>
#include <rfal_rfst25r3916.h>
#include <st_errno.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <vector>

#include "boards/wt32_sc01_plus_rev_a.hpp"
#include "diagnostics/build_info.hpp"
#include "diagnostics/shared_i2c_diagnostic.hpp"
#include "hardware/display/wt32_display.hpp"
#include "hardware/scale/nau7802_device.hpp"
#include "nfc/formats/openprinttag/codec.hpp"
#include "nfc/formats/openprinttag/initializer.hpp"
#include "nfc/protocols/nfcv/tag.hpp"

// Arduino-ESP32 2.0.17 creates loopTask with 8 KiB by default and exposes this
// supported strong override for sketches whose setup()/loop() call chains need
// more room. The OpenPrintTag decode path is compiler-measured separately in CI.
constexpr std::size_t diagnostic_loop_task_stack_bytes = 16384U;
constexpr std::size_t diagnostic_loop_task_stack_safety_bytes = 4096U;
constexpr std::size_t diagnostic_http_task_stack_bytes = 12288U;
constexpr std::size_t diagnostic_http_task_stack_safety_bytes = 4096U;
constexpr std::size_t diagnostic_http_response_capacity = 3072U;
constexpr bool diagnostic_initialization_write_enabled = false;
SET_LOOP_TASK_STACK_SIZE(diagnostic_loop_task_stack_bytes);
static_assert(
    diagnostic_loop_task_stack_bytes > diagnostic_loop_task_stack_safety_bytes);
static_assert(
    diagnostic_http_task_stack_bytes > diagnostic_http_task_stack_safety_bytes);

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
constexpr std::uint32_t stable_uid_rounds_before_read = 2U;
constexpr std::uint32_t memory_read_duration_ms = 30000U;
constexpr std::size_t maximum_memory_bytes =
    opentag::nfc::nfcv::WritePlan::maximum_image_size;
constexpr std::size_t maximum_blocks_per_request = 8U;
constexpr std::size_t system_information_buffer_size = 64U;
constexpr std::size_t read_response_buffer_size =
    1U + maximum_blocks_per_request * RFAL_NFCV_MAX_BLOCK_LEN;
constexpr std::size_t slix2_physical_bytes = 320U;
constexpr std::size_t slix2_block_size = 4U;
constexpr std::size_t slix2_block_count = 80U;
constexpr std::size_t initialization_usable_bytes = 312U;
constexpr std::size_t initialization_auxiliary_requested_bytes = 32U;
constexpr std::size_t initialization_auxiliary_payload_offset = 234U;
constexpr std::size_t initialization_auxiliary_tag_offset = 276U;
constexpr std::size_t initialization_auxiliary_encoded_bytes = 35U;
constexpr std::size_t initialization_last_block = 77U;
constexpr std::uint32_t initialization_reference_checksum = 0x6B6EABF1U;
constexpr std::uint32_t initialization_duration_ms = 60000U;
constexpr const char* access_point_ssid = "OpenTag-I2C-Test";
constexpr const char* access_point_url = "http://192.168.4.1";

constexpr std::uint8_t st25r3916b_operation_control_register = 0x02U;
constexpr std::uint8_t st25r3916b_main_irq_register = 0x1AU;
constexpr std::uint8_t st25r3916b_last_irq_register = 0x1DU;
constexpr std::uint8_t st25r3916b_enable_oscillator = 0x80U;
constexpr std::uint8_t st25r3916b_irq_oscillator_stable = 0x80U;
static_assert(maximum_memory_bytes == 4096U);
static_assert(initialization_usable_bytes / slix2_block_size == 78U);
static_assert(initialization_last_block + 1U ==
              initialization_usable_bytes / slix2_block_size);
static_assert(initialization_auxiliary_tag_offset % slix2_block_size == 0U);
static_assert(initialization_auxiliary_tag_offset +
                  initialization_auxiliary_encoded_bytes ==
              initialization_usable_bytes - 1U);

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

void log_task_stack_high_water(const char* checkpoint) {
  Serial.printf(
      "task stack checkpoint=%s task=%s free=%lu bytes\n",
      checkpoint,
      pcTaskGetName(nullptr),
      static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)));
}

std::unique_ptr<opentag::nfc::openprinttag::DecodedTag>
decode_openprinttag_off_stack(core::ByteView image) {
  auto decoded = std::unique_ptr<opentag::nfc::openprinttag::DecodedTag>(
      new (std::nothrow) opentag::nfc::openprinttag::DecodedTag());
  if (!decoded) return nullptr;
  const auto result = opentag::nfc::openprinttag::Codec::decode(image, *decoded);
  return result.ok() ? std::move(decoded) : nullptr;
}

enum class BlockReadResult : std::uint8_t {
  pass,
  retryable_command_error,
  fatal_response_error,
};

enum class InitializationState : std::uint8_t {
  unavailable,
  ready,
  queued,
  running,
  refused,
  pass,
  fail,
};

const char* to_string(InitializationState value) {
  switch (value) {
    case InitializationState::unavailable: return "UNAVAILABLE";
    case InitializationState::ready: return "READY";
    case InitializationState::queued: return "QUEUED";
    case InitializationState::running: return "RUNNING";
    case InitializationState::refused: return "REFUSED";
    case InitializationState::pass: return "PASS";
    case InitializationState::fail: return "FAIL";
  }
  return "FAIL";
}

struct InitializationStatus {
  InitializationState state{InitializationState::unavailable};
  CheckResult image_generation{CheckResult::pending};
  CheckResult reference_vector{CheckResult::pending};
  CheckResult current_decode{CheckResult::pending};
  CheckResult authorization{CheckResult::pending};
  CheckResult preflight{CheckResult::pending};
  CheckResult preflight_transport{CheckResult::pending};
  CheckResult block_security{CheckResult::pending};
  CheckResult block_write{CheckResult::pending};
  CheckResult block_verify{CheckResult::pending};
  CheckResult full_image_verify{CheckResult::pending};
  CheckResult post_write_decode{CheckResult::pending};
  CheckResult post_write_transport{CheckResult::pending};
  CheckResult rf_field_disable{CheckResult::pending};
  std::array<char, 24U> uid{};
  std::array<char, 24U> uid_after_write{};
  std::array<char, 9U> before_checksum{};
  std::array<char, 9U> generated_checksum{};
  std::array<char, 9U> target_checksum{};
  std::array<char, 9U> final_checksum{};
  std::array<char, 112U> message{};
  FailureStage failure_stage{FailureStage::none};
  std::int32_t failed_block{-1};
  std::uint16_t differing_blocks{0U};
  std::uint16_t planned_blocks{0U};
  std::uint16_t written_blocks{0U};
  bool blank{false};
};

struct InitializationRequest {
  std::array<char, 24U> uid{};
  std::array<char, 9U> checksum{};
};

struct PreviewWorkspace {
  InitializationStatus status;
  Snapshot snapshot;
};

constexpr char diagnostic_page[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>OpenTag NFC-V Read / OpenPrintTag Preview</title>
  <style>
    :root{font-family:system-ui,sans-serif;color:#e9f4f5;background:#10191c}
    body{margin:0;padding:1rem}main{max-width:48rem;margin:auto}
    h1{font-size:1.6rem;margin:.25rem 0}.note{color:#9fb4ba;margin:.3rem 0 1rem}
    dl{display:grid;grid-template-columns:minmax(12rem,1fr) 1fr;gap:.45rem 1rem;
       padding:1rem;border:1px solid #345057;border-radius:.75rem;background:#172327}
    dt{color:#9fb4ba}dd{margin:0;font-family:ui-monospace,monospace;overflow-wrap:anywhere}
    .pass{color:#69d591}.fail{color:#ff8d7f}.pending{color:#f4ce68}
    section{margin-top:1.2rem;padding:1rem;border:1px solid #5c4c26;border-radius:.75rem;background:#211d14}
    button,a.action{display:inline-block;margin:.35rem .35rem .35rem 0;padding:.65rem 1rem;border:0;border-radius:.5rem;background:#167d82;color:white;font-weight:700;text-decoration:none}
  </style>
</head>
<body><main>
  <h1>Dual I2C / NFC-V Diagnostic</h1>
  <p class="note">Scale: GPIO10/GPIO11 · NFC: GPIO13/GPIO14 · IRQ GPIO12 · 100 kHz each. Boot and normal operation remain read-only.</p>
  <dl id="results"><dt>Status</dt><dd class="pending">Loading…</dd></dl>
  <button id="refresh" type="button">Refresh now</button>
  <section>
    <h2>Read-only OpenPrintTag preview</h2>
    <p class="note">The one-time initialization control was removed after physical acceptance. This diagnostic does not expose an NFC write action.</p>
    <dl id="initialization"><dt>Status</dt><dd class="pending">Waiting for the read-only diagnostic…</dd></dl>
    <a class="action" href="/api/v1/openprinttag/image" download>Download reference image</a>
  </section>
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
      system_information_result:'System information',geometry_result:'Tag geometry',
      block_count:'Block count',block_size:'Block size',memory_capacity_bytes:'Memory capacity (bytes)',
      first_memory_read_result:'First memory read',second_memory_read_result:'Second memory read',
      memory_bytes_read:'Raw bytes read',memory_read_consistency_result:'Read consistency',
      memory_checksum:'Memory checksum (FNV-1a)',memory_uid:'Memory image UID',
      memory_dump_available:'Raw dump available',
      nfc_bus_error_count:'NFC bus errors',nau_communication_result:'NAU communication',
      scale_after_test:'Scale after test',nfc_after_scale:'NFC after scale',
      scale_sample_count:'Scale samples',last_raw:'Last raw reading',phase:'Phase',failure_stage:'First failing stage'};
    const initLabels={state:'OpenPrintTag preview',uid:'Tag UID',block_count:'Physical blocks',block_size:'Block size',
      memory_capacity_bytes:'Physical bytes',usable_bytes:'OpenPrintTag usable bytes',generated_bytes:'Generated bytes',
      before_checksum:'Before checksum',generated_checksum:'Generated checksum',target_checksum:'Target full checksum',final_checksum:'Final full checksum',
      differing_blocks:'Differing blocks',planned_blocks:'Reference plan blocks',
      mime_type:'MIME type',auxiliary_region:'Auxiliary region',
      auxiliary_requested_bytes:'Auxiliary requested bytes',auxiliary_encoded_bytes:'Auxiliary encoded bytes',
      auxiliary_tag_offset:'Auxiliary tag offset',blank:'Writable range blank',
      image_generation:'Image generation',reference_vector:'Reference vector',current_decode:'Current OpenPrintTag decode',
      failure_stage:'First failing stage',message:'Message'};
    const order=Object.keys(labels);let timer;
    function text(v){return Array.isArray(v)?(v.length?v.join(' '):'none'):String(v)}
    function cls(k,v){v=String(v);return v==='PASS'||v==='ACK'||v==='HIGH'||v==='true'||
      (k==='tag_detected'&&(v==='YES'||v==='NO'))?'pass':
      v==='FAIL'||v==='BUS ERROR'||v==='LOW'||(k==='blank'&&v==='false')||v==='true-invalid'?'fail':'pending'}
    async function load(){clearTimeout(timer);try{
      const r=await fetch('/api/v1/i2c-diagnostic',{cache:'no-store'});if(!r.ok)throw Error(r.status);
      const d=await r.json(),root=document.getElementById('results');root.replaceChildren();
      for(const k of order){const dt=document.createElement('dt'),dd=document.createElement('dd');
        dt.textContent=labels[k];const value=d[k];
        dd.textContent=text(value);dd.className=cls(k,value);root.append(dt,dd)}
      if(d.complete)await loadPreview();timer=setTimeout(load,5000);
    }catch(e){document.getElementById('results').textContent='Diagnostic endpoint unavailable: '+e;timer=setTimeout(load,5000)}}
    async function loadPreview(){try{
      const r=await fetch('/api/v1/openprinttag/preview',{cache:'no-store'}),d=await r.json();
      const root=document.getElementById('initialization');root.replaceChildren();
      for(const k of Object.keys(initLabels)){const dt=document.createElement('dt'),dd=document.createElement('dd');
        dt.textContent=initLabels[k];dd.textContent=text(d[k]??'-');dd.className=cls(k,d[k]);root.append(dt,dd)}
    }catch(e){document.getElementById('initialization').textContent='Preview unavailable: '+e}}
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
    log_task_stack_high_water("setup entry");
    Serial.printf(
        "OpenTag dual I2C diagnostic %s (%s)\n",
        OPENTAG_PROJECT_VERSION,
        OPENTAG_GIT_SHA);
    Serial.printf(
        "loopTask stack configured=%u bytes safety budget=%u bytes\n",
        static_cast<unsigned>(diagnostic_loop_task_stack_bytes),
        static_cast<unsigned>(diagnostic_loop_task_stack_safety_bytes));

    initialize_screen();
    log_task_stack_high_water("after initialize_screen");
    log_task_stack_high_water("before prepare_initialization_image");
    prepare_initialization_image();
    publish_and_render(true);
    start_web_server();
    log_task_stack_high_water("after web server startup");
    characterize_bus();
  }

  void loop() {
    const auto now_ms = millis();
    if constexpr (diagnostic_initialization_write_enabled) {
      InitializationRequest request;
      if (take_initialization_request(request)) {
        run_initialization(request);
      }
    }
    if (live_.phase == Phase::coexistence) poll_coexistence(now_ms);
    if (elapsed(now_ms, last_render_ms_, 250U)) publish_and_render(false);
    delay(2U);
  }

 private:
  void prepare_initialization_image() {
    InitializationStatus status;
    const auto generated = opentag::nfc::openprinttag::Initializer::generate({
        initialization_usable_bytes,
        slix2_block_size,
        initialization_auxiliary_requested_bytes,
        std::nullopt,
    });
    log_task_stack_high_water("after image generation");
    const auto decoded = generated.ok()
        ? decode_openprinttag_off_stack(core::ByteView(generated.value().bytes))
        : nullptr;
    log_task_stack_high_water("after startup Codec::decode");
    if (!generated.ok() || generated.value().bytes.size() != initialization_usable_bytes ||
        !generated.value().auxiliary_region_offset.has_value() ||
        *generated.value().auxiliary_region_offset !=
            initialization_auxiliary_payload_offset ||
        !decoded || !decoded->envelope.auxiliary.has_value() ||
        decoded->envelope.auxiliary->absolute_offset !=
            initialization_auxiliary_tag_offset ||
        decoded->envelope.auxiliary->size !=
            initialization_auxiliary_encoded_bytes ||
        decoded->envelope.auxiliary->used_size != 1U) {
      status.state = InitializationState::fail;
      status.image_generation = CheckResult::fail;
      status.reference_vector = CheckResult::skipped;
      status.failure_stage = FailureStage::image_generation;
      publish_initialization_status(status);
      record_failure(FailureStage::image_generation);
      return;
    }
    status.image_generation = CheckResult::pass;
    const auto checksum = diagnostic_checksum(
        generated.value().bytes.data(), generated.value().bytes.size());
    status.generated_checksum = format_diagnostic_checksum(checksum);
    if (checksum != initialization_reference_checksum) {
      status.state = InitializationState::fail;
      status.reference_vector = CheckResult::fail;
      status.failure_stage = FailureStage::reference_vector_mismatch;
      publish_initialization_status(status);
      record_failure(FailureStage::reference_vector_mismatch);
      return;
    }
    std::copy(
        generated.value().bytes.begin(),
        generated.value().bytes.end(),
        initialization_image_.begin());
    initialization_image_ready_ = true;
    status.state = InitializationState::unavailable;
    status.reference_vector = CheckResult::pass;
    publish_initialization_status(status);
  }

  void record_initialization_failure(
      InitializationStatus& status,
      FailureStage stage,
      std::int32_t block = -1) {
    if (status.failure_stage == FailureStage::none) status.failure_stage = stage;
    if (status.failed_block < 0 && block >= 0) status.failed_block = block;
    status.state = InitializationState::fail;
    record_failure(stage);
    publish_initialization_status(status);
  }

  void publish_initialization_status(const InitializationStatus& status) {
    portENTER_CRITICAL(&initialization_mux_);
    initialization_status_ = status;
    portEXIT_CRITICAL(&initialization_mux_);
  }

  void copy_initialization_status(InitializationStatus& status) const {
    portENTER_CRITICAL(&initialization_mux_);
    status = initialization_status_;
    portEXIT_CRITICAL(&initialization_mux_);
  }

  bool take_initialization_request(InitializationRequest& request) {
    bool pending = false;
    portENTER_CRITICAL(&initialization_mux_);
    if (initialization_request_pending_) {
      request = initialization_request_;
      initialization_request_pending_ = false;
      pending = true;
    }
    portEXIT_CRITICAL(&initialization_mux_);
    return pending;
  }

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
    skip_pending_memory_results();
  }

  void skip_pending_memory_results() {
    if (live_.system_information_result == CheckResult::pending) {
      live_.system_information_result = CheckResult::skipped;
    }
    if (live_.geometry_result == CheckResult::pending) {
      live_.geometry_result = CheckResult::skipped;
    }
    if (live_.first_memory_read_result == CheckResult::pending) {
      live_.first_memory_read_result = CheckResult::skipped;
    }
    if (live_.second_memory_read_result == CheckResult::pending) {
      live_.second_memory_read_result = CheckResult::skipped;
    }
    if (live_.memory_read_consistency_result == CheckResult::pending) {
      live_.memory_read_consistency_result = CheckResult::skipped;
    }
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
      skip_pending_memory_results();
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
      skip_pending_memory_results();
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

  bool post_rf_transport_healthy(
      FailureStage probe_stage,
      FailureStage identity_stage) {
    if (probe_nfc_target(st25r3916b_address) != ProbeResult::ack) {
      record_failure(probe_stage);
      return false;
    }

    std::uint8_t raw_identity = 0U;
    if (!nfc_read_register(st25r3916b_identity_register, raw_identity)) {
      record_failure(identity_stage);
      return false;
    }
    const auto identity = decode_st25r3916b_identity(raw_identity);
    if (!identity.is_st25r3916b()) {
      record_failure(identity_stage);
      return false;
    }
    return true;
  }

  bool sample_scale_once() {
    const auto ready = scale_adc_.sample_ready();
    if (!ready.ok()) {
      ++live_.scale_bus_error_count;
      scale_runtime_failed_ = true;
      record_failure(FailureStage::scale_sample);
      return false;
    }
    if (!ready.value()) return true;

    const auto raw = scale_adc_.read_raw();
    if (!raw.ok()) {
      ++live_.scale_bus_error_count;
      scale_runtime_failed_ = true;
      record_failure(FailureStage::scale_sample);
      return false;
    }
    live_.last_raw = raw.value();
    ++live_.scale_sample_count;
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

  bool read_system_information(
      const std::uint8_t* wire_uid,
      NfcvSystemInformation& information) {
    std::array<std::uint8_t, system_information_buffer_size> response{};
    std::uint16_t received = 0U;
    auto result = nfc_.rfalNfcvPollerGetSystemInformation(
        static_cast<std::uint8_t>(RFAL_NFCV_REQ_FLAG_DEFAULT),
        wire_uid,
        response.data(),
        static_cast<std::uint16_t>(response.size()),
        &received);
    if (result == ERR_NONE &&
        parse_nfcv_system_information(response.data(), received, false, information) &&
        information.memory_size_present) {
      return true;
    }
    if (is_rfal_i2c_error(result)) note_rfal_error(result);

    response.fill(0U);
    received = 0U;
    result = nfc_.rfalNfcvPollerExtendedGetSystemInformation(
        static_cast<std::uint8_t>(RFAL_NFCV_REQ_FLAG_DEFAULT),
        wire_uid,
        static_cast<std::uint8_t>(RFAL_NFCV_SYSINFO_REQ_ALL),
        response.data(),
        static_cast<std::uint16_t>(response.size()),
        &received);
    if (result == ERR_NONE &&
        parse_nfcv_system_information(response.data(), received, true, information) &&
        information.memory_size_present) {
      return true;
    }
    if (result != ERR_NONE) note_rfal_error(result);
    return false;
  }

  BlockReadResult read_blocks(
      const std::uint8_t* wire_uid,
      std::uint16_t first_block,
      std::size_t requested_blocks,
      std::size_t block_size,
      std::uint8_t* destination,
      FailureStage read_failure_stage = FailureStage::nfcv_memory_read) {
    std::array<std::uint8_t, read_response_buffer_size> response{};
    std::uint16_t received = 0U;
    ReturnCode result = ERR_PARAM;

    if (requested_blocks == 1U) {
      if (first_block <= 0xFFU) {
        result = nfc_.rfalNfcvPollerReadSingleBlock(
            static_cast<std::uint8_t>(RFAL_NFCV_REQ_FLAG_DEFAULT),
            wire_uid,
            static_cast<std::uint8_t>(first_block),
            response.data(),
            static_cast<std::uint16_t>(response.size()),
            &received);
      } else {
        result = nfc_.rfalNfcvPollerExtendedReadSingleBlock(
            static_cast<std::uint8_t>(RFAL_NFCV_REQ_FLAG_DEFAULT),
            wire_uid,
            first_block,
            response.data(),
            static_cast<std::uint16_t>(response.size()),
            &received);
      }
    } else {
      // ELECHOUSE forwards this value directly onto the ISO15693 wire, where
      // the field encodes the requested block count minus one.
      const auto encoded_count = requested_blocks - 1U;
      const bool standard_addressing =
          first_block <= 0xFFU &&
          static_cast<std::size_t>(first_block) + requested_blocks <= 0x100U;
      if (standard_addressing) {
        result = nfc_.rfalNfcvPollerReadMultipleBlocks(
            static_cast<std::uint8_t>(RFAL_NFCV_REQ_FLAG_DEFAULT),
            wire_uid,
            static_cast<std::uint8_t>(first_block),
            static_cast<std::uint8_t>(encoded_count),
            response.data(),
            static_cast<std::uint16_t>(response.size()),
            &received);
      } else {
        result = nfc_.rfalNfcvPollerExtendedReadMultipleBlocks(
            static_cast<std::uint8_t>(RFAL_NFCV_REQ_FLAG_DEFAULT),
            wire_uid,
            first_block,
            static_cast<std::uint16_t>(encoded_count),
            response.data(),
            static_cast<std::uint16_t>(response.size()),
            &received);
      }
    }

    if (result != ERR_NONE) {
      note_rfal_error(result);
      return BlockReadResult::retryable_command_error;
    }
    const auto response_result = copy_nfcv_read_response(
        response.data(),
        received,
        destination,
        requested_blocks * block_size);
    if (response_result == ReadResponseResult::wrong_length) {
      record_failure(
          read_failure_stage == FailureStage::nfcv_memory_read
              ? FailureStage::nfcv_response_length
              : read_failure_stage);
      Serial.printf(
          "NFC-V read response length %u, expected %u\n",
          static_cast<unsigned>(received),
          static_cast<unsigned>(requested_blocks * block_size + 1U));
      return BlockReadResult::fatal_response_error;
    }
    if (response_result != ReadResponseResult::pass) {
      record_failure(read_failure_stage);
      return BlockReadResult::fatal_response_error;
    }
    return BlockReadResult::pass;
  }

  bool read_complete_memory(
      const std::uint8_t* wire_uid,
      const opentag::nfc::nfcv::TagGeometry& geometry,
      std::array<std::uint8_t, maximum_memory_bytes>& image,
      std::uint32_t operation_started_ms,
      FailureStage read_failure_stage = FailureStage::nfcv_memory_read,
      std::uint32_t duration_ms = memory_read_duration_ms) {
    std::size_t block = 0U;
    std::size_t preferred_request_blocks = maximum_blocks_per_request;
    while (block < geometry.block_count) {
      if (elapsed(millis(), operation_started_ms, duration_ms)) {
        record_failure(read_failure_stage);
        Serial.println("NFC-V memory read exceeded its bounded duration");
        return false;
      }
      std::size_t request_blocks = std::min(
          preferred_request_blocks,
          geometry.block_count - block);
      if (block <= 0xFFU) {
        request_blocks = std::min(request_blocks, 0x100U - block);
      }

      bool read = false;
      bool fallback_used = false;
      while (request_blocks > 0U) {
        const auto destination_offset = block * geometry.block_size;
        const auto read_result = read_blocks(
            wire_uid,
            static_cast<std::uint16_t>(block),
            request_blocks,
            geometry.block_size,
            image.data() + destination_offset,
            read_failure_stage);
        read = read_result == BlockReadResult::pass;
        if (read) break;
        if (read_result == BlockReadResult::fatal_response_error) return false;
        if (request_blocks == 1U) break;
        fallback_used = true;
        request_blocks = std::max<std::size_t>(1U, request_blocks / 2U);
        Serial.printf(
            "NFC-V multi-block read fallback: block %u, count %u\n",
            static_cast<unsigned>(block),
            static_cast<unsigned>(request_blocks));
      }
      if (!read) {
        record_failure(read_failure_stage);
        return false;
      }
      if (fallback_used) preferred_request_blocks = request_blocks;
      block += request_blocks;
      (void)sample_scale_once();
    }
    return true;
  }

  bool confirm_single_uid(
      const std::uint8_t* expected_wire_uid,
      FailureStage failure_stage = FailureStage::nfcv_uid_changed_during_read) {
    rfalNfcvInventoryRes presence{};
    const auto presence_result = nfc_.rfalNfcvPollerCheckPresence(&presence);
    if (presence_result != ERR_NONE) {
      if (presence_result != ERR_TIMEOUT) note_rfal_error(presence_result);
      record_failure(failure_stage);
      return false;
    }

    std::array<rfalNfcvListenDevice, RFAL_NFC_MAX_DEVICES> devices{};
    std::uint8_t device_count = 0U;
    const auto inventory_result = nfc_.rfalNfcvPollerCollisionResolution(
        RFAL_COMPLIANCE_MODE_NFC,
        static_cast<std::uint8_t>(devices.size()),
        devices.data(),
        &device_count);
    if (inventory_result != ERR_NONE) {
      note_rfal_error(inventory_result);
      record_failure(failure_stage);
      return false;
    }
    if (device_count != 1U ||
        std::memcmp(devices[0].InvRes.UID, expected_wire_uid, RFAL_NFCV_UID_LEN) != 0) {
      record_failure(failure_stage);
      return false;
    }
    return true;
  }

  void print_memory_dump(std::size_t length) const {
    Serial.println("NFC-V raw memory dump (read-only):");
    for (std::size_t offset = 0U; offset < length; offset += 16U) {
      Serial.printf("%04X: ", static_cast<unsigned>(offset));
      const auto line_length = std::min<std::size_t>(16U, length - offset);
      for (std::size_t index = 0U; index < line_length; ++index) {
        Serial.printf("%02X%s", memory_image_first_[offset + index],
                      index + 1U == line_length ? "" : " ");
      }
      Serial.println();
    }
  }

  bool run_memory_read_diagnostic(const std::uint8_t* expected_wire_uid) {
    memory_read_attempted_ = true;
    if (reference_uid_.has_value()) {
      live_.memory_uid = format_diagnostic_uid(reference_uid_->bytes);
    }
    set_phase(Phase::memory_read);
    ScopedRfField field(reader_);
    bool operation_ok = false;

    const auto field_on = field.enable();
    if (field_on != ERR_NONE) {
      note_rfal_error(field_on);
      live_.rf_field_result = CheckResult::fail;
      record_failure(FailureStage::rf_field_on);
      skip_pending_memory_results();
    } else {
      if (live_.rf_field_result != CheckResult::fail) {
        live_.rf_field_result = CheckResult::pass;
      }
      operation_ok = [&]() {
        const auto operation_started_ms = millis();
        NfcvSystemInformation information;
        if (!read_system_information(expected_wire_uid, information)) {
          live_.system_information_result = CheckResult::fail;
          record_failure(FailureStage::nfcv_system_information);
          skip_pending_memory_results();
          return false;
        }
        live_.system_information_result = CheckResult::pass;

        if (std::memcmp(
                information.wire_uid.data(),
                expected_wire_uid,
                information.wire_uid.size()) != 0) {
          live_.geometry_result = CheckResult::skipped;
          record_failure(FailureStage::nfcv_uid_changed_during_read);
          skip_pending_memory_results();
          return false;
        }

        const opentag::nfc::nfcv::TagGeometry geometry{
            information.block_size,
            information.block_count};
        const auto geometry_valid = geometry.validate();
        if (!geometry_valid.ok() || geometry.block_size > RFAL_NFCV_MAX_BLOCK_LEN ||
            geometry.capacity() > maximum_memory_bytes) {
          live_.geometry_result = CheckResult::fail;
          record_failure(FailureStage::nfcv_invalid_geometry);
          Serial.println("NFC-V geometry is invalid, unsupported, or exceeds 4096 bytes");
          skip_pending_memory_results();
          return false;
        }
        live_.geometry_result = CheckResult::pass;
        live_.block_count = static_cast<std::uint32_t>(geometry.block_count);
        live_.block_size = static_cast<std::uint16_t>(geometry.block_size);
        live_.memory_capacity_bytes = static_cast<std::uint32_t>(geometry.capacity());

        if (!read_complete_memory(
                expected_wire_uid,
                geometry,
                memory_image_first_,
                operation_started_ms)) {
          live_.first_memory_read_result = CheckResult::fail;
          skip_pending_memory_results();
          return false;
        }
        live_.first_memory_read_result = CheckResult::pass;
        live_.memory_bytes_read = static_cast<std::uint32_t>(geometry.capacity());

        if (!confirm_single_uid(expected_wire_uid)) {
          live_.second_memory_read_result = CheckResult::skipped;
          live_.memory_read_consistency_result = CheckResult::skipped;
          return false;
        }

        if (!read_complete_memory(
                expected_wire_uid,
                geometry,
                memory_image_second_,
                operation_started_ms)) {
          live_.second_memory_read_result = CheckResult::fail;
          live_.memory_read_consistency_result = CheckResult::skipped;
          return false;
        }
        live_.second_memory_read_result = CheckResult::pass;
        if (!confirm_single_uid(expected_wire_uid)) {
          live_.memory_read_consistency_result = CheckResult::skipped;
          return false;
        }
        if (std::memcmp(
                memory_image_first_.data(),
                memory_image_second_.data(),
                geometry.capacity()) != 0) {
          live_.memory_read_consistency_result = CheckResult::fail;
          record_failure(FailureStage::nfcv_read_consistency);
          return false;
        }
        live_.memory_read_consistency_result = CheckResult::pass;
        live_.memory_checksum = format_diagnostic_checksum(
            diagnostic_checksum(memory_image_first_.data(), geometry.capacity()));
        return true;
      }();
    }

    const auto field_off = field.close();
    if (field_off != ERR_NONE) {
      note_rfal_error(field_off);
      live_.rf_field_result = CheckResult::fail;
      record_failure(FailureStage::rf_field_off);
      operation_ok = false;
    }

    const bool transport_healthy = post_rf_transport_healthy(
        FailureStage::nfc_post_read_transport,
        FailureStage::nfc_post_read_transport);
    if (!transport_healthy) operation_ok = false;

    if (operation_ok) {
      live_.memory_dump_available = true;
      publish_snapshot();
      memory_dump_available_.store(true, std::memory_order_release);
      print_memory_dump(live_.memory_bytes_read);
    }
    set_phase(Phase::coexistence);
    return operation_ok;
  }

  std::array<std::uint8_t, RFAL_NFCV_UID_LEN> wire_uid_for(
      const opentag::nfc::nfcv::Uid& uid) const {
    std::array<std::uint8_t, RFAL_NFCV_UID_LEN> wire{};
    std::reverse_copy(uid.bytes.begin(), uid.bytes.end(), wire.begin());
    return wire;
  }

  bool read_block_security(
      const std::uint8_t* wire_uid,
      std::uint16_t block,
      std::size_t block_size,
      bool& locked,
      bool& data_matches,
      const std::uint8_t* expected_data) {
    std::array<std::uint8_t, 2U + RFAL_NFCV_MAX_BLOCK_LEN> response{};
    std::uint16_t received = 0U;
    const auto flags = static_cast<std::uint8_t>(
        RFAL_NFCV_REQ_FLAG_DEFAULT | RFAL_NFCV_REQ_FLAG_OPTION);
    ReturnCode result = ERR_PARAM;
    if (block <= 0xFFU) {
      result = nfc_.rfalNfcvPollerReadSingleBlock(
          flags,
          wire_uid,
          static_cast<std::uint8_t>(block),
          response.data(),
          static_cast<std::uint16_t>(response.size()),
          &received);
    } else {
      result = nfc_.rfalNfcvPollerExtendedReadSingleBlock(
          flags,
          wire_uid,
          block,
          response.data(),
          static_cast<std::uint16_t>(response.size()),
          &received);
    }
    if (result != ERR_NONE) {
      note_rfal_error(result);
      return false;
    }
    if (received != block_size + 2U || (response[0] & 0x01U) != 0U) {
      Serial.printf(
          "NFC-V block %u security response length/flags invalid (%u, 0x%02X)\n",
          static_cast<unsigned>(block),
          static_cast<unsigned>(received),
          static_cast<unsigned>(response[0]));
      return false;
    }
    // ISO/IEC 15693 block security status bit 0 is the permanent lock bit.
    locked = (response[1] & 0x01U) != 0U;
    data_matches = expected_data == nullptr ||
        std::memcmp(response.data() + 2U, expected_data, block_size) == 0;
    if (!data_matches) {
      Serial.printf("NFC-V block %u changed during lock preflight\n",
                    static_cast<unsigned>(block));
    }
    return true;
  }

  ReturnCode write_block_once(
      const std::uint8_t* wire_uid,
      const opentag::nfc::nfcv::BlockWrite& block) {
    if (block.block_index <= 0xFFU) {
      return nfc_.rfalNfcvPollerWriteSingleBlock(
          static_cast<std::uint8_t>(RFAL_NFCV_REQ_FLAG_DEFAULT),
          wire_uid,
          static_cast<std::uint8_t>(block.block_index),
          block.data.data(),
          static_cast<std::uint8_t>(block.data.size()));
    }
    return nfc_.rfalNfcvPollerExtendedWriteSingleBlock(
        static_cast<std::uint8_t>(RFAL_NFCV_REQ_FLAG_DEFAULT),
        wire_uid,
        block.block_index,
        block.data.data(),
        static_cast<std::uint8_t>(block.data.size()));
  }

  void run_initialization(const InitializationRequest& request) {
    InitializationStatus status;
    status.state = InitializationState::running;
    status.image_generation = initialization_image_ready_
        ? CheckResult::pass : CheckResult::fail;
    status.reference_vector = initialization_image_ready_
        ? CheckResult::pass : CheckResult::fail;
    status.generated_checksum =
        format_diagnostic_checksum(initialization_reference_checksum);
    status.uid = request.uid;
    status.before_checksum = request.checksum;
    status.authorization = CheckResult::pass;
    std::snprintf(status.message.data(), status.message.size(),
                  "Fresh hardware preflight running; no bytes written yet");
    publish_initialization_status(status);
    set_phase(Phase::initialization);

    bool operation_ok = false;
    ScopedRfField field(reader_);
    const auto field_on = field.enable();
    if (field_on != ERR_NONE) {
      note_rfal_error(field_on);
      status.preflight = CheckResult::fail;
      record_initialization_failure(status, FailureStage::rf_field_on);
    } else {
      operation_ok = [&]() {
        if (!initialization_image_ready_ || !reference_uid_.has_value()) {
          status.authorization = CheckResult::fail;
          record_initialization_failure(status, FailureStage::write_authorization);
          return false;
        }
        const auto expected_uid = format_diagnostic_uid(reference_uid_->bytes);
        if (reference_uid_->bytes[0] != 0xE0U ||
            reference_uid_->bytes[1] != 0x04U ||
            reference_uid_->bytes[2] != 0x01U ||
            std::strcmp(expected_uid.data(), request.uid.data()) != 0) {
          status.authorization = CheckResult::fail;
          record_initialization_failure(status, FailureStage::write_authorization);
          return false;
        }

        const auto wire_uid = wire_uid_for(*reference_uid_);
        if (!confirm_single_uid(
                wire_uid.data(), FailureStage::uid_changed_before_write)) {
          status.preflight = CheckResult::fail;
          record_initialization_failure(status, FailureStage::uid_changed_before_write);
          return false;
        }

        NfcvSystemInformation information;
        if (!read_system_information(wire_uid.data(), information)) {
          status.preflight = CheckResult::fail;
          record_initialization_failure(
              status, FailureStage::nfcv_system_information);
          return false;
        }
        if (std::memcmp(
                information.wire_uid.data(), wire_uid.data(), wire_uid.size()) != 0) {
          status.preflight = CheckResult::fail;
          record_initialization_failure(
              status, FailureStage::uid_changed_before_write);
          return false;
        }
        if (!matches_initialization_geometry(
                information, slix2_block_count, slix2_block_size)) {
          status.preflight = CheckResult::fail;
          record_initialization_failure(status, FailureStage::geometry_changed);
          return false;
        }
        const opentag::nfc::nfcv::TagGeometry geometry{
            information.block_size, information.block_count};
        const auto geometry_valid = geometry.validate();
        if (!geometry_valid.ok() || geometry.capacity() != slix2_physical_bytes) {
          status.preflight = CheckResult::fail;
          record_initialization_failure(status, FailureStage::geometry_changed);
          return false;
        }

        const auto started_ms = millis();
        initialization_before_image_.fill(0U);
        if (!read_complete_memory(
                wire_uid.data(),
                geometry,
                initialization_before_image_,
                started_ms,
                FailureStage::full_image_verify,
                initialization_duration_ms)) {
          status.preflight = CheckResult::fail;
          record_initialization_failure(status, FailureStage::full_image_verify);
          return false;
        }
        if (!confirm_single_uid(
                wire_uid.data(), FailureStage::uid_changed_before_write)) {
          status.preflight = CheckResult::fail;
          record_initialization_failure(
              status, FailureStage::uid_changed_before_write);
          return false;
        }
        const auto before_checksum = format_diagnostic_checksum(diagnostic_checksum(
            initialization_before_image_.data(), geometry.capacity()));
        if (std::strcmp(before_checksum.data(), request.checksum.data()) != 0) {
          status.authorization = CheckResult::fail;
          status.preflight = CheckResult::fail;
          record_initialization_failure(status, FailureStage::write_authorization);
          return false;
        }
        if (!is_zero_filled(core::ByteView(
                initialization_before_image_.data(), initialization_usable_bytes))) {
          status.blank = false;
          status.preflight = CheckResult::fail;
          status.state = InitializationState::refused;
          status.failure_stage = FailureStage::tag_not_blank;
          std::snprintf(status.message.data(), status.message.size(),
                        "INITIALIZATION REFUSED: TAG IS NOT BLANK");
          record_failure(FailureStage::tag_not_blank);
          publish_initialization_status(status);
          return false;
        }
        status.blank = true;

        const auto preflight_field_off = field.close();
        if (preflight_field_off != ERR_NONE) {
          note_rfal_error(preflight_field_off);
          status.rf_field_disable = CheckResult::fail;
          record_initialization_failure(status, FailureStage::rf_field_off);
          return false;
        }
        status.rf_field_disable = CheckResult::pass;
        if (!post_rf_transport_healthy(
                FailureStage::nfc_post_read_transport,
                FailureStage::nfc_post_read_transport)) {
          status.preflight_transport = CheckResult::fail;
          record_initialization_failure(
              status, FailureStage::nfc_post_read_transport);
          return false;
        }
        status.preflight_transport = CheckResult::pass;
        const auto write_field_on = field.enable();
        if (write_field_on != ERR_NONE) {
          note_rfal_error(write_field_on);
          status.preflight = CheckResult::fail;
          record_initialization_failure(status, FailureStage::rf_field_on);
          return false;
        }

        const auto target = build_initialization_target(
            core::ByteView(initialization_before_image_.data(), geometry.capacity()),
            core::ByteView(initialization_image_.data(), initialization_image_.size()));
        if (!target.ok()) {
          status.preflight = CheckResult::fail;
          record_initialization_failure(status, FailureStage::image_generation);
          return false;
        }
        status.target_checksum = format_diagnostic_checksum(diagnostic_checksum(
            target.value().data(), target.value().size()));
        const auto unlocked_plan = opentag::nfc::nfcv::WritePlan::build(
            core::ByteView(initialization_before_image_.data(), geometry.capacity()),
            core::ByteView(target.value()),
            geometry);
        if (!unlocked_plan.ok()) {
          status.preflight = CheckResult::fail;
          record_initialization_failure(status, FailureStage::image_generation);
          return false;
        }
        status.differing_blocks = static_cast<std::uint16_t>(unlocked_plan.value().blocks().size());

        std::vector<bool> locks(geometry.block_count, false);
        for (const auto& block : unlocked_plan.value().blocks()) {
          bool locked = false;
          bool data_matches = false;
          const auto offset =
              static_cast<std::size_t>(block.block_index) * geometry.block_size;
          if (block.block_index > initialization_last_block ||
              !read_block_security(
                  wire_uid.data(),
                  block.block_index,
                  geometry.block_size,
                  locked,
                  data_matches,
                  initialization_before_image_.data() + offset)) {
            status.block_security = CheckResult::fail;
            record_initialization_failure(
                status,
                FailureStage::tag_locked_write_protected,
                block.block_index);
            return false;
          }
          if (!data_matches) {
            status.preflight = CheckResult::fail;
            status.block_security = CheckResult::fail;
            record_initialization_failure(
                status, FailureStage::full_image_verify, block.block_index);
            return false;
          }
          locks[block.block_index] = locked;
          if (locked) {
            status.block_security = CheckResult::fail;
            record_initialization_failure(
                status,
                FailureStage::tag_locked_write_protected,
                block.block_index);
            return false;
          }
          (void)sample_scale_once();
        }
        status.block_security = CheckResult::pass;

        const auto plan = opentag::nfc::nfcv::WritePlan::build(
            core::ByteView(initialization_before_image_.data(), geometry.capacity()),
            core::ByteView(target.value()),
            geometry,
            locks);
        if (!plan.ok()) {
          record_initialization_failure(
              status, FailureStage::tag_locked_write_protected);
          return false;
        }
        status.planned_blocks = static_cast<std::uint16_t>(plan.value().blocks().size());
        status.preflight = CheckResult::pass;
        status.block_write = CheckResult::pending;
        status.block_verify = CheckResult::pending;
        std::snprintf(status.message.data(), status.message.size(),
                      "Preflight PASS; bounded block transaction running");
        publish_initialization_status(status);

        for (const auto& block : plan.value().blocks()) {
          if (elapsed(millis(), started_ms, initialization_duration_ms)) {
            status.block_write = CheckResult::fail;
            record_initialization_failure(
                status, FailureStage::block_write, block.block_index);
            return false;
          }
          if (!confirm_single_uid(
                  wire_uid.data(), FailureStage::uid_changed_before_write)) {
            record_initialization_failure(
                status, FailureStage::uid_changed_before_write, block.block_index);
            return false;
          }
          const auto written = write_block_once(wire_uid.data(), block);
          if (written != ERR_NONE) {
            note_rfal_error(written);
            const auto stage = written == ERR_WRITE || written == ERR_REQUEST
                ? FailureStage::tag_locked_write_protected
                : FailureStage::block_write;
            status.block_write = CheckResult::fail;
            record_initialization_failure(status, stage, block.block_index);
            return false;
          }
          ++status.written_blocks;
          status.block_write = CheckResult::pass;

          std::array<std::uint8_t, RFAL_NFCV_MAX_BLOCK_LEN> verified{};
          if (read_blocks(
                  wire_uid.data(),
                  block.block_index,
                  1U,
                  geometry.block_size,
                  verified.data(),
                  FailureStage::block_verify) != BlockReadResult::pass ||
              std::memcmp(verified.data(), block.data.data(), geometry.block_size) != 0) {
            status.block_verify = CheckResult::fail;
            record_initialization_failure(
                status, FailureStage::block_verify, block.block_index);
            return false;
          }
          status.block_verify = CheckResult::pass;
          publish_initialization_status(status);
          (void)sample_scale_once();
        }

        initialization_after_image_.fill(0U);
        if (!confirm_single_uid(
                wire_uid.data(), FailureStage::full_image_verify)) {
          status.full_image_verify = CheckResult::fail;
          record_initialization_failure(status, FailureStage::full_image_verify);
          return false;
        }
        if (!read_complete_memory(
                wire_uid.data(),
                geometry,
                initialization_after_image_,
                started_ms,
                FailureStage::full_image_verify,
                initialization_duration_ms)) {
          status.full_image_verify = CheckResult::fail;
          record_initialization_failure(status, FailureStage::full_image_verify);
          return false;
        }
        if (!confirm_single_uid(
                wire_uid.data(), FailureStage::full_image_verify) ||
            std::memcmp(
                initialization_after_image_.data(),
                target.value().data(),
                geometry.capacity()) != 0) {
          status.full_image_verify = CheckResult::fail;
          record_initialization_failure(status, FailureStage::full_image_verify);
          return false;
        }
        status.full_image_verify = CheckResult::pass;
        status.uid_after_write = expected_uid;
        status.final_checksum = format_diagnostic_checksum(diagnostic_checksum(
            initialization_after_image_.data(), geometry.capacity()));

        log_task_stack_high_water("before post-write Codec::decode");
        const auto decoded = decode_openprinttag_off_stack(core::ByteView(
            initialization_after_image_.data(), geometry.capacity()));
        log_task_stack_high_water("after post-write Codec::decode");
        if (!decoded ||
            decoded->envelope.capability_capacity != initialization_usable_bytes ||
            decoded->envelope.meta.used_size != 4U ||
            decoded->envelope.main.used_size != 1U ||
            !decoded->envelope.auxiliary.has_value() ||
            decoded->envelope.auxiliary->absolute_offset !=
                initialization_auxiliary_tag_offset ||
            decoded->envelope.auxiliary->size !=
                initialization_auxiliary_encoded_bytes ||
            decoded->envelope.auxiliary->used_size != 1U ||
            decoded->material.unknown_main_fields != 0U ||
            decoded->material.unknown_auxiliary_fields != 0U) {
          status.post_write_decode = CheckResult::fail;
          record_initialization_failure(status, FailureStage::post_write_decode);
          return false;
        }
        status.post_write_decode = CheckResult::pass;
        status.current_decode = CheckResult::pass;
        return true;
      }();
    }

    const auto field_off = field.close();
    if (field_off != ERR_NONE) {
      note_rfal_error(field_off);
      status.rf_field_disable = CheckResult::fail;
      record_initialization_failure(status, FailureStage::rf_field_off);
      operation_ok = false;
    } else if (status.rf_field_disable != CheckResult::fail) {
      status.rf_field_disable = field_on == ERR_NONE
          ? CheckResult::pass : CheckResult::skipped;
    }
    const bool transport_healthy = post_rf_transport_healthy(
        FailureStage::post_write_transport,
        FailureStage::post_write_transport);
    status.post_write_transport = transport_healthy
        ? CheckResult::pass : CheckResult::fail;
    if (!transport_healthy) {
      record_initialization_failure(status, FailureStage::post_write_transport);
      operation_ok = false;
    }

    if (operation_ok) {
      status.state = InitializationState::pass;
      status.failure_stage = FailureStage::none;
      std::snprintf(status.message.data(), status.message.size(),
                    "Blank tag initialized and fully verified; no automatic retry performed");
      std::copy_n(
          initialization_after_image_.begin(),
          slix2_physical_bytes,
          memory_image_first_.begin());
      std::copy_n(
          initialization_after_image_.begin(),
          slix2_physical_bytes,
          memory_image_second_.begin());
      live_.memory_checksum = status.final_checksum;
      live_.memory_dump_available = true;
      memory_dump_available_.store(true, std::memory_order_release);
      publish_initialization_status(status);
    } else if (status.state == InitializationState::running) {
      status.state = InitializationState::fail;
    }
    publish_initialization_status(status);
    set_phase(Phase::complete);
    Serial.printf("OpenPrintTag initialize  %s\n", to_string(status.state));
    Serial.printf("Blocks planned/written  %u/%u\n",
                  static_cast<unsigned>(status.planned_blocks),
                  static_cast<unsigned>(status.written_blocks));
    Serial.printf("Final checksum          %s\n",
                  status.final_checksum[0] == '\0' ? "-" : status.final_checksum.data());
    Serial.printf("Initialization failure %s\n", to_string(status.failure_stage));
    if (status.failed_block >= 0) {
      Serial.printf("Failed block            %ld\n", static_cast<long>(status.failed_block));
    }
    print_final_report();
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
    if (live_.rf_field_result != CheckResult::fail) {
      live_.rf_field_result = CheckResult::pass;
    }

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

    const bool transport_healthy = post_rf_transport_healthy(
        FailureStage::nfc_post_inventory_probe,
        FailureStage::nfc_post_inventory_identity);
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

    if (!record_inventory(devices, device_count)) return false;
    if (!memory_read_attempted_ && device_count == 1U &&
        live_.matching_uid_round_count >= stable_uid_rounds_before_read) {
      return run_memory_read_diagnostic(devices[0].InvRes.UID);
    }
    return true;
  }

  void poll_coexistence(std::uint32_t now_ms) {
    live_.coexistence_elapsed_ms = std::min<std::uint32_t>(
        static_cast<std::uint32_t>(now_ms - coexistence_started_ms_),
        coexistence_duration_ms);

    if (elapsed(now_ms, last_scale_check_ms_, scale_check_interval_ms)) {
      last_scale_check_ms_ = now_ms;
      (void)sample_scale_once();
    }

    if (elapsed(now_ms, last_inventory_ms_, inventory_interval_ms)) {
      last_inventory_ms_ = now_ms;
      if (!run_inventory_round()) {
        nfc_runtime_failed_ = true;
      }
    }

    if (!elapsed(now_ms, coexistence_started_ms_, coexistence_duration_ms)) return;

    if (!post_rf_transport_healthy(
            FailureStage::nfc_coexistence_probe,
            FailureStage::nfc_coexistence_probe)) {
      nfc_runtime_failed_ = true;
    }
    if (!memory_read_attempted_) skip_pending_memory_results();
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
    Serial.printf("System information    %s\n", to_string(live_.system_information_result));
    Serial.printf("Tag geometry          %s\n", to_string(live_.geometry_result));
    Serial.printf("Block count           %lu\n", static_cast<unsigned long>(live_.block_count));
    Serial.printf("Block size            %u\n", static_cast<unsigned>(live_.block_size));
    Serial.printf("Memory capacity       %lu\n", static_cast<unsigned long>(live_.memory_capacity_bytes));
    Serial.printf("First memory read     %s\n", to_string(live_.first_memory_read_result));
    Serial.printf("Second memory read    %s\n", to_string(live_.second_memory_read_result));
    Serial.printf("Raw bytes read        %lu\n", static_cast<unsigned long>(live_.memory_bytes_read));
    Serial.printf("Read consistency      %s\n", to_string(live_.memory_read_consistency_result));
    Serial.printf("Memory checksum       %s\n",
                  live_.memory_checksum[0] == '\0' ? "-" : live_.memory_checksum.data());
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

  void copy_snapshot(Snapshot& copy) const {
    portENTER_CRITICAL(&snapshot_mux_);
    copy = published_;
    portEXIT_CRITICAL(&snapshot_mux_);
  }

  Snapshot copy_snapshot() const {
    Snapshot copy;
    copy_snapshot(copy);
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
    draw_result_line(150, "UID", snapshot.uid[0] == '\0' ? "-" : snapshot.uid.data(), TFT_LIGHTGREY);
    draw_result_line(164, "SYSTEM INFORMATION", to_string(snapshot.system_information_result),
                     result_color(snapshot.system_information_result));
    std::snprintf(
        value, sizeof(value), "%lu x %u = %lu",
        static_cast<unsigned long>(snapshot.block_count),
        static_cast<unsigned>(snapshot.block_size),
        static_cast<unsigned long>(snapshot.memory_capacity_bytes));
    draw_result_line(178, "GEOMETRY BLOCKS x BYTES", value,
                     result_color(snapshot.geometry_result));
    draw_result_line(192, "FIRST MEMORY READ", to_string(snapshot.first_memory_read_result),
                     result_color(snapshot.first_memory_read_result));
    draw_result_line(206, "SECOND MEMORY READ", to_string(snapshot.second_memory_read_result),
                     result_color(snapshot.second_memory_read_result));
    draw_result_line(220, "READ CONSISTENCY", to_string(snapshot.memory_read_consistency_result),
                     result_color(snapshot.memory_read_consistency_result));
    std::snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(snapshot.nfc_bus_error_count));
    draw_result_line(234, "NFC BUS ERRORS", value,
                     snapshot.nfc_bus_error_count == 0U ? TFT_GREEN : TFT_RED);
    std::snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(snapshot.scale_bus_error_count));
    draw_result_line(248, "SCALE BUS ERRORS", value,
                     snapshot.scale_bus_error_count == 0U ? TFT_GREEN : TFT_RED);

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

  void preview_initialization_status(
      InitializationStatus& status,
      Snapshot& snapshot) const {
    copy_initialization_status(status);
    if (status.state != InitializationState::unavailable) return;
    copy_snapshot(snapshot);
    if (!initialization_image_ready_ ||
        snapshot.phase != Phase::complete ||
        !memory_dump_available_.load(std::memory_order_acquire) ||
        snapshot.memory_bytes_read != slix2_physical_bytes ||
        snapshot.block_count != slix2_block_count ||
        snapshot.block_size != slix2_block_size) {
      std::snprintf(status.message.data(), status.message.size(),
                    "Complete the read-only 80 x 4 NFC-V diagnostic first");
      return;
    }

    status.state = InitializationState::ready;
    status.uid = snapshot.memory_uid;
    status.before_checksum = snapshot.memory_checksum;
    status.blank = is_zero_filled(core::ByteView(
        memory_image_first_.data(), initialization_usable_bytes));
    if (status.blank) {
      status.current_decode = CheckResult::skipped;
    } else {
      const auto current_decoded = decode_openprinttag_off_stack(
          core::ByteView(memory_image_first_.data(), slix2_physical_bytes));
      status.current_decode = current_decoded
          ? CheckResult::pass : CheckResult::fail;
      log_task_stack_high_water("httpd after OpenPrintTag decode");
    }
    const auto target = build_initialization_target(
        core::ByteView(memory_image_first_.data(), slix2_physical_bytes),
        core::ByteView(initialization_image_.data(), initialization_image_.size()));
    log_task_stack_high_water("httpd after target generation");
    if (!target.ok()) {
      status.state = InitializationState::fail;
      status.failure_stage = FailureStage::image_generation;
      std::snprintf(status.message.data(), status.message.size(),
                    "Generated OpenPrintTag image does not fit this tag");
      return;
    }
    const auto plan = opentag::nfc::nfcv::WritePlan::build(
        core::ByteView(memory_image_first_.data(), slix2_physical_bytes),
        core::ByteView(target.value()),
        {slix2_block_size, slix2_block_count});
    log_task_stack_high_water("httpd after WritePlan generation");
    if (!plan.ok()) {
      status.state = InitializationState::fail;
      status.failure_stage = FailureStage::image_generation;
      std::snprintf(status.message.data(), status.message.size(),
                    "Unable to build bounded initialization plan");
      return;
    }
    status.differing_blocks = static_cast<std::uint16_t>(plan.value().blocks().size());
    status.planned_blocks = status.differing_blocks;
    status.target_checksum = format_diagnostic_checksum(diagnostic_checksum(
        target.value().data(), target.value().size()));
    std::snprintf(
        status.message.data(), status.message.size(), "%s",
        status.blank
            ? "Blank tag detected; diagnostic write action is disabled"
            : "Nonblank OpenPrintTag image detected; diagnostic is read-only");
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

  static esp_err_t send_http_workspace_failure(httpd_req_t* request) {
    httpd_resp_set_status(request, "503 Service Unavailable");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(
        request,
        "{\"message\":\"Diagnostic HTTP workspace allocation failed\"}");
  }

  static esp_err_t api_handler(httpd_req_t* request) {
    log_task_stack_high_water("httpd api_handler entry");
    auto* firmware = static_cast<DualI2cFirmware*>(request->user_ctx);
    auto snapshot_storage = std::unique_ptr<Snapshot>(new (std::nothrow) Snapshot());
    auto json = std::unique_ptr<char[]>(
        new (std::nothrow) char[diagnostic_http_response_capacity]());
    if (!snapshot_storage || !json) return send_http_workspace_failure(request);
    firmware->copy_snapshot(*snapshot_storage);
    const auto& snapshot = *snapshot_storage;
    std::size_t used = 0U;
    append_json(
        json.get(), diagnostic_http_response_capacity, used,
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
        "\"system_information_result\":\"%s\",\"geometry_result\":\"%s\","
        "\"block_count\":%lu,\"block_size\":%u,\"memory_capacity_bytes\":%lu,"
        "\"first_memory_read_result\":\"%s\",\"second_memory_read_result\":\"%s\","
        "\"memory_bytes_read\":%lu,\"memory_read_consistency_result\":\"%s\","
        "\"memory_checksum\":\"%s\",\"memory_uid\":\"%s\","
        "\"memory_dump_available\":%s,"
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
        to_string(snapshot.system_information_result),
        to_string(snapshot.geometry_result),
        static_cast<unsigned long>(snapshot.block_count),
        static_cast<unsigned>(snapshot.block_size),
        static_cast<unsigned long>(snapshot.memory_capacity_bytes),
        to_string(snapshot.first_memory_read_result),
        to_string(snapshot.second_memory_read_result),
        static_cast<unsigned long>(snapshot.memory_bytes_read),
        to_string(snapshot.memory_read_consistency_result),
        snapshot.memory_checksum.data(),
        snapshot.memory_uid.data(),
        snapshot.memory_dump_available ? "true" : "false",
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
    log_task_stack_high_water("httpd api_handler before response send");
    return httpd_resp_send(request, json.get(), static_cast<ssize_t>(used));
  }

  static esp_err_t preview_handler(httpd_req_t* request) {
    log_task_stack_high_water("httpd preview_handler entry");
    auto* firmware = static_cast<DualI2cFirmware*>(request->user_ctx);
    auto workspace = std::unique_ptr<PreviewWorkspace>(
        new (std::nothrow) PreviewWorkspace());
    auto json = std::unique_ptr<char[]>(
        new (std::nothrow) char[diagnostic_http_response_capacity]());
    if (!workspace || !json) return send_http_workspace_failure(request);
    log_task_stack_high_water("httpd before preview_initialization_status");
    firmware->preview_initialization_status(
        workspace->status, workspace->snapshot);
    log_task_stack_high_water("httpd after preview_initialization_status");
    const auto& status = workspace->status;
    std::size_t used = 0U;
    append_json(
        json.get(), diagnostic_http_response_capacity, used,
        "{\"state\":\"%s\",\"uid\":\"%s\",\"uid_after_write\":\"%s\","
        "\"block_count\":%u,\"block_size\":%u,\"memory_capacity_bytes\":%u,"
        "\"usable_bytes\":%u,\"generated_bytes\":%u,"
        "\"before_checksum\":\"%s\",\"generated_checksum\":\"%s\","
        "\"target_checksum\":\"%s\","
        "\"final_checksum\":\"%s\",\"differing_blocks\":%u,"
        "\"planned_blocks\":%u,\"written_blocks\":%u,"
        "\"mime_type\":\"%s\","
        "\"auxiliary_region\":\"32-byte minimum; 35-byte aligned region\","
        "\"auxiliary_requested_bytes\":%u,\"auxiliary_encoded_bytes\":%u,"
        "\"auxiliary_tag_offset\":%u,"
        "\"blank\":%s,\"image_generation\":\"%s\","
        "\"reference_vector\":\"%s\",\"current_decode\":\"%s\","
        "\"authorization\":\"%s\","
        "\"preflight\":\"%s\",\"preflight_transport\":\"%s\","
        "\"block_security\":\"%s\","
        "\"block_write\":\"%s\",\"block_verify\":\"%s\","
        "\"full_image_verify\":\"%s\",\"post_write_decode\":\"%s\","
        "\"post_write_transport\":\"%s\",\"rf_field_disable\":\"%s\","
        "\"failed_block\":%ld,"
        "\"failure_stage\":\"%s\",\"message\":\"%s\"}",
        to_string(status.state),
        status.uid.data(),
        status.uid_after_write.data(),
        static_cast<unsigned>(slix2_block_count),
        static_cast<unsigned>(slix2_block_size),
        static_cast<unsigned>(slix2_physical_bytes),
        static_cast<unsigned>(initialization_usable_bytes),
        static_cast<unsigned>(initialization_usable_bytes),
        status.before_checksum.data(),
        status.generated_checksum.data(),
        status.target_checksum.data(),
        status.final_checksum.data(),
        static_cast<unsigned>(status.differing_blocks),
        static_cast<unsigned>(status.planned_blocks),
        static_cast<unsigned>(status.written_blocks),
        opentag::nfc::openprinttag::mime_type,
        static_cast<unsigned>(initialization_auxiliary_requested_bytes),
        static_cast<unsigned>(initialization_auxiliary_encoded_bytes),
        static_cast<unsigned>(initialization_auxiliary_tag_offset),
        status.blank ? "true" : "false",
        to_string(status.image_generation),
        to_string(status.reference_vector),
        to_string(status.current_decode),
        to_string(status.authorization),
        to_string(status.preflight),
        to_string(status.preflight_transport),
        to_string(status.block_security),
        to_string(status.block_write),
        to_string(status.block_verify),
        to_string(status.full_image_verify),
        to_string(status.post_write_decode),
        to_string(status.post_write_transport),
        to_string(status.rf_field_disable),
        static_cast<long>(status.failed_block),
        to_string(status.failure_stage),
        status.message.data());
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    log_task_stack_high_water("httpd preview_handler before response send");
    return httpd_resp_send(request, json.get(), static_cast<ssize_t>(used));
  }

  static esp_err_t initialization_image_handler(httpd_req_t* request) {
    auto* firmware = static_cast<DualI2cFirmware*>(request->user_ctx);
    if (!firmware->initialization_image_ready_) {
      httpd_resp_set_status(request, "503 Service Unavailable");
      return httpd_resp_sendstr(request, "OpenPrintTag image generation failed.\n");
    }
    httpd_resp_set_type(request, "application/octet-stream");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(
        request,
        "Content-Disposition",
        "attachment; filename=openprinttag-nfcv-initialization-312-aux32.bin");
    httpd_resp_set_hdr(request, "X-OpenPrintTag-Reference",
                       opentag::nfc::openprinttag::initializer_reference_revision);
    httpd_resp_set_hdr(request, "X-OpenTag-Checksum-FNV1A32", "6B6EABF1");
    httpd_resp_set_hdr(
        request, "X-OpenPrintTag-Auxiliary-Requested-Bytes", "32");
    httpd_resp_set_hdr(
        request, "X-OpenPrintTag-Auxiliary-Encoded-Bytes", "35");
    httpd_resp_set_hdr(request, "X-OpenPrintTag-Auxiliary-Offset", "276");
    return httpd_resp_send(
        request,
        reinterpret_cast<const char*>(firmware->initialization_image_.data()),
        static_cast<ssize_t>(firmware->initialization_image_.size()));
  }

  static esp_err_t initialize_handler(httpd_req_t* request) {
    auto* firmware = static_cast<DualI2cFirmware*>(request->user_ctx);
    if (request->content_len <= 0 || request->content_len > 512) {
      httpd_resp_set_status(request, "400 Bad Request");
      httpd_resp_set_type(request, "application/json");
      return httpd_resp_sendstr(
          request,
          "{\"failure_stage\":\"WRITE AUTHORIZATION\",\"message\":\"Invalid request body\"}");
    }
    std::array<char, 513U> body{};
    std::size_t received_total = 0U;
    while (received_total < static_cast<std::size_t>(request->content_len)) {
      const int received = httpd_req_recv(
          request,
          body.data() + received_total,
          static_cast<std::size_t>(request->content_len) - received_total);
      if (received <= 0) {
        httpd_resp_set_status(request, "408 Request Timeout");
        return httpd_resp_sendstr(request, "{\"message\":\"Request body timeout\"}");
      }
      received_total += static_cast<std::size_t>(received);
    }

    JsonDocument document;
    const auto parsed = deserializeJson(document, body.data(), received_total);
    const char* supplied_uid = document["uid"] | static_cast<const char*>(nullptr);
    const char* supplied_checksum =
        document["expected_before_checksum"] | static_cast<const char*>(nullptr);
    const char* supplied_confirmation =
        document["confirmation"] | static_cast<const char*>(nullptr);
    auto preview_workspace = std::unique_ptr<PreviewWorkspace>(
        new (std::nothrow) PreviewWorkspace());
    if (!preview_workspace) return send_http_workspace_failure(request);
    firmware->preview_initialization_status(
        preview_workspace->status, preview_workspace->snapshot);
    const auto& preview = preview_workspace->status;
    if (parsed || preview.state != InitializationState::ready) {
      httpd_resp_set_status(request, "409 Conflict");
      httpd_resp_set_type(request, "application/json");
      return httpd_resp_sendstr(
          request,
          "{\"failure_stage\":\"WRITE AUTHORIZATION\",\"message\":\"Read-only diagnostic is not ready for initialization\"}");
    }
    if (!preview.blank) {
      httpd_resp_set_status(request, "409 Conflict");
      httpd_resp_set_type(request, "application/json");
      return httpd_resp_sendstr(
          request,
          "{\"failure_stage\":\"TAG NOT BLANK\",\"message\":\"INITIALIZATION REFUSED: TAG IS NOT BLANK\"}");
    }
    const auto authorization = validate_initialization_authorization(
        supplied_uid,
        supplied_checksum,
        supplied_confirmation,
        preview.uid.data(),
        preview.before_checksum.data());
    if (authorization != InitializationAuthorizationResult::pass ||
        supplied_checksum == nullptr ||
        std::strcmp(supplied_checksum, "97B79EC5") != 0) {
      httpd_resp_set_status(request, "403 Forbidden");
      httpd_resp_set_type(request, "application/json");
      return httpd_resp_sendstr(
          request,
          "{\"failure_stage\":\"WRITE AUTHORIZATION\",\"message\":\"UID, expected-before checksum 97B79EC5, or exact confirmation did not match\"}");
    }

    InitializationRequest queued;
    std::snprintf(queued.uid.data(), queued.uid.size(), "%s", supplied_uid);
    std::snprintf(
        queued.checksum.data(), queued.checksum.size(), "%s", supplied_checksum);
    bool accepted = false;
    portENTER_CRITICAL(&firmware->initialization_mux_);
    if (!firmware->initialization_request_pending_ &&
        firmware->initialization_status_.state != InitializationState::queued &&
        firmware->initialization_status_.state != InitializationState::running &&
        firmware->initialization_status_.state != InitializationState::pass) {
      firmware->initialization_request_ = queued;
      firmware->initialization_request_pending_ = true;
      firmware->initialization_status_ = preview;
      firmware->initialization_status_.state = InitializationState::queued;
      firmware->initialization_status_.authorization = CheckResult::pass;
      std::snprintf(
          firmware->initialization_status_.message.data(),
          firmware->initialization_status_.message.size(),
          "Authorized request queued for fresh hardware preflight");
      accepted = true;
    }
    portEXIT_CRITICAL(&firmware->initialization_mux_);
    if (!accepted) {
      httpd_resp_set_status(request, "409 Conflict");
      httpd_resp_set_type(request, "application/json");
      return httpd_resp_sendstr(
          request,
          "{\"failure_stage\":\"WRITE AUTHORIZATION\",\"message\":\"Initialization is already queued, running, or completed\"}");
    }

    httpd_resp_set_status(request, "202 Accepted");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(
        request,
        "{\"state\":\"QUEUED\",\"message\":\"Fresh preflight will run before the first write\"}");
  }

  static esp_err_t dump_handler(httpd_req_t* request) {
    auto* firmware = static_cast<DualI2cFirmware*>(request->user_ctx);
    if (!firmware->memory_dump_available_.load(std::memory_order_acquire)) {
      httpd_resp_set_status(request, "409 Conflict");
      httpd_resp_set_type(request, "text/plain; charset=utf-8");
      httpd_resp_set_hdr(request, "Cache-Control", "no-store");
      return httpd_resp_sendstr(
          request,
          "A consistent NFC-V memory image is not available.\n");
    }

    const auto snapshot = firmware->copy_snapshot();
    std::array<char, 16U> block_count{};
    std::array<char, 16U> block_size{};
    std::array<char, 16U> content_length{};
    std::snprintf(block_count.data(), block_count.size(), "%lu",
                  static_cast<unsigned long>(snapshot.block_count));
    std::snprintf(block_size.data(), block_size.size(), "%u",
                  static_cast<unsigned>(snapshot.block_size));
    std::snprintf(content_length.data(), content_length.size(), "%lu",
                  static_cast<unsigned long>(snapshot.memory_bytes_read));
    httpd_resp_set_type(request, "application/octet-stream");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(
        request,
        "Content-Disposition",
        "attachment; filename=opentag-nfcv-memory.bin");
    httpd_resp_set_hdr(request, "X-OpenTag-NFCV-UID", snapshot.memory_uid.data());
    httpd_resp_set_hdr(request, "X-OpenTag-NFCV-Block-Count", block_count.data());
    httpd_resp_set_hdr(request, "X-OpenTag-NFCV-Block-Size", block_size.data());
    httpd_resp_set_hdr(request, "X-OpenTag-NFCV-Checksum-FNV1A32", snapshot.memory_checksum.data());
    httpd_resp_set_hdr(request, "X-OpenTag-NFCV-Bytes", content_length.data());
    return httpd_resp_send(
        request,
        reinterpret_cast<const char*>(firmware->memory_image_first_.data()),
        static_cast<ssize_t>(snapshot.memory_bytes_read));
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
    config.max_uri_handlers = diagnostic_initialization_write_enabled ? 6U : 5U;
    config.max_open_sockets = 2U;
    config.lru_purge_enable = true;
    config.stack_size = diagnostic_http_task_stack_bytes;
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
    httpd_uri_t dump{};
    dump.uri = "/api/v1/nfcv-dump";
    dump.method = HTTP_GET;
    dump.handler = dump_handler;
    dump.user_ctx = this;
    httpd_uri_t preview{};
    preview.uri = "/api/v1/openprinttag/preview";
    preview.method = HTTP_GET;
    preview.handler = preview_handler;
    preview.user_ctx = this;
    httpd_uri_t initialization_image{};
    initialization_image.uri = "/api/v1/openprinttag/image";
    initialization_image.method = HTTP_GET;
    initialization_image.handler = initialization_image_handler;
    initialization_image.user_ctx = this;
    if (httpd_register_uri_handler(server_, &root) != ESP_OK ||
        httpd_register_uri_handler(server_, &api) != ESP_OK ||
        httpd_register_uri_handler(server_, &dump) != ESP_OK ||
        httpd_register_uri_handler(server_, &preview) != ESP_OK ||
        httpd_register_uri_handler(server_, &initialization_image) != ESP_OK) {
      Serial.println("Diagnostic HTTP routes failed to register");
      httpd_stop(server_);
      server_ = nullptr;
      web_ready_ = false;
      return;
    }
    if constexpr (diagnostic_initialization_write_enabled) {
      httpd_uri_t initialize{};
      initialize.uri = "/api/v1/openprinttag/initialize";
      initialize.method = HTTP_POST;
      initialize.handler = initialize_handler;
      initialize.user_ctx = this;
      if (httpd_register_uri_handler(server_, &initialize) != ESP_OK) {
        Serial.println("Diagnostic initialization route failed to register");
        httpd_stop(server_);
        server_ = nullptr;
        web_ready_ = false;
        return;
      }
    }
    Serial.printf(
        "Diagnostic HTTP task stack configured=%u bytes safety budget=%u bytes writes=%s\n",
        static_cast<unsigned>(diagnostic_http_task_stack_bytes),
        static_cast<unsigned>(diagnostic_http_task_stack_safety_bytes),
        diagnostic_initialization_write_enabled ? "ENABLED" : "DISABLED");
    Serial.printf("Diagnostic browser page: %s (%s)\n", access_point_url, access_point_ssid);
  }

  Wt32DisplayDevice screen_{false};
  Nau7802Device scale_adc_;
  RfalRfST25R3916Class reader_;
  RfalNfcClass nfc_;
  Snapshot live_;
  mutable Snapshot published_;
  std::array<std::uint8_t, maximum_memory_bytes> memory_image_first_{};
  std::array<std::uint8_t, maximum_memory_bytes> memory_image_second_{};
  std::array<std::uint8_t, initialization_usable_bytes> initialization_image_{};
  std::array<std::uint8_t, maximum_memory_bytes> initialization_before_image_{};
  std::array<std::uint8_t, maximum_memory_bytes> initialization_after_image_{};
  std::atomic<bool> memory_dump_available_{false};
  mutable portMUX_TYPE snapshot_mux_ = portMUX_INITIALIZER_UNLOCKED;
  mutable portMUX_TYPE initialization_mux_ = portMUX_INITIALIZER_UNLOCKED;
  InitializationStatus initialization_status_;
  InitializationRequest initialization_request_;
  bool initialization_request_pending_{false};
  httpd_handle_t server_{nullptr};
  bool screen_ready_{false};
  bool web_ready_{false};
  bool scale_wire_ready_{false};
  bool nfc_wire_ready_{false};
  bool scale_runtime_failed_{false};
  bool nfc_runtime_failed_{false};
  bool memory_read_attempted_{false};
  bool initialization_image_ready_{false};
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
