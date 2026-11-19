#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <string>

namespace hyprshare::error {

struct RetryConfig {
  std::uint32_t max_retries = 3;
  std::uint32_t retry_delay_ms = 1000;
  double backoff_multiplier = 2.0;
  bool retry_on_timeout = true;
  bool retry_on_network_error = true;
};

struct RecoveryState {
  std::string operation;
  std::uint32_t attempts = 0;
  std::int64_t last_attempt = 0;
  std::int64_t next_retry = 0;
  bool recovered = false;
};

using RetryableOperation = std::function<std::expected<void, std::string>()>;

class RetryManager {
 public:
  RetryManager();
  ~RetryManager();

  void set_config(const RetryConfig& config);
  RetryConfig config() const;

  std::expected<void, std::string> execute_with_retry(const std::string& operation, RetryableOperation func);
  std::expected<void, std::string> execute_with_backoff(const std::string& operation, RetryableOperation func);

  RecoveryState last_state() const;

 private:
  RetryConfig config_;
  RecoveryState last_state_;
};

}  // namespace hyprshare::error
