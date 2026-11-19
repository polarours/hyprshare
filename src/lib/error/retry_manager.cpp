#include "hyprshare/error/retry_manager.hpp"

#include <chrono>
#include <thread>

namespace hyprshare::error {

RetryManager::RetryManager() {}

RetryManager::~RetryManager() {}

void RetryManager::set_config(const RetryConfig& config) {
  config_ = config;
}

RetryConfig RetryManager::config() const {
  return config_;
}

std::expected<void, std::string> RetryManager::execute_with_retry(
  const std::string& operation, RetryableOperation func) {
  last_state_.operation = operation;
  last_state_.attempts = 0;
  last_state_.recovered = false;

  for (std::uint32_t attempt = 0; attempt <= config_.max_retries; ++attempt) {
    last_state_.attempts = attempt + 1;
    last_state_.last_attempt = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();

    auto result = func();
    if (result.has_value()) {
      last_state_.recovered = true;
      return {};
    }

    if (attempt < config_.max_retries) {
      std::this_thread::sleep_for(std::chrono::milliseconds(config_.retry_delay_ms));
    }
  }

  return std::unexpected("operation failed after " + std::to_string(config_.max_retries + 1) + " attempts: " + operation);
}

std::expected<void, std::string> RetryManager::execute_with_backoff(
  const std::string& operation, RetryableOperation func) {
  last_state_.operation = operation;
  last_state_.attempts = 0;
  last_state_.recovered = false;

  std::uint32_t delay_ms = config_.retry_delay_ms;

  for (std::uint32_t attempt = 0; attempt <= config_.max_retries; ++attempt) {
    last_state_.attempts = attempt + 1;
    last_state_.last_attempt = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();

    auto result = func();
    if (result.has_value()) {
      last_state_.recovered = true;
      return {};
    }

    if (attempt < config_.max_retries) {
      std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
      delay_ms = static_cast<std::uint32_t>(delay_ms * config_.backoff_multiplier);
    }
  }

  return std::unexpected("operation failed after " + std::to_string(config_.max_retries + 1) + " attempts: " + operation);
}

RecoveryState RetryManager::last_state() const {
  return last_state_;
}

}  // namespace hyprshare::error
