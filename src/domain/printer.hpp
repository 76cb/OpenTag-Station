#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "domain/spool.hpp"

namespace opentag::domain {

enum class PrinterState {
  unknown,
  idle,
  printing,
  paused,
  attention,
  finished,
  stopped,
  error,
  offline,
  not_configured,
};

[[nodiscard]] constexpr bool is_active_print_state(PrinterState state) {
  return state == PrinterState::printing || state == PrinterState::paused ||
      state == PrinterState::attention;
}

struct Toolhead {
  std::string printer_id;
  int backend_id{0};
  int display_number{1};
  std::string display_name{"T1"};
  std::optional<SpoolId> assigned_spool;

  static Toolhead from_zero_based_backend(std::string printer, int backend) {
    Toolhead result;
    result.printer_id = std::move(printer);
    result.backend_id = backend;
    result.display_number = backend + 1;
    result.display_name = "T" + std::to_string(result.display_number);
    return result;
  }
};

struct Printer {
  std::string id;
  std::string display_name;
  PrinterState state{PrinterState::unknown};
  std::string raw_state;
  std::vector<Toolhead> toolheads;
};

}  // namespace opentag::domain
