#pragma once

#include <optional>
#include <utility>
#include <variant>

#include "core/error.hpp"

namespace opentag::core {

template <typename T>
class Result {
 public:
  static Result success(T value) { return Result(std::move(value)); }
  static Result failure(Error error) { return Result(std::move(error)); }

  [[nodiscard]] bool ok() const { return std::holds_alternative<T>(value_); }
  [[nodiscard]] const T& value() const { return std::get<T>(value_); }
  [[nodiscard]] const Error& error() const { return std::get<Error>(value_); }

 private:
  explicit Result(T value) : value_(std::move(value)) {}
  explicit Result(Error error) : value_(std::move(error)) {}

  std::variant<T, Error> value_;
};

template <>
class Result<void> {
 public:
  static Result success() { return Result(); }
  static Result failure(Error error) { return Result(std::move(error)); }

  [[nodiscard]] bool ok() const { return !error_.has_value(); }
  [[nodiscard]] const Error& error() const { return *error_; }

 private:
  Result() = default;
  explicit Result(Error error) : error_(std::move(error)) {}

  std::optional<Error> error_;
};

}  // namespace opentag::core
