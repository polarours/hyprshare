#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace hyprshare::perf {

class BufferPool {
 public:
  explicit BufferPool(std::size_t buffer_size = 8192, std::size_t pool_size = 16);
  ~BufferPool();

  std::shared_ptr<std::vector<char>> acquire();
  void release(std::shared_ptr<std::vector<char>> buffer);

  std::size_t buffer_size() const;
  std::size_t available() const;
  std::size_t total() const;

 private:
  std::size_t buffer_size_;
  std::vector<std::shared_ptr<std::vector<char>>> pool_;
  std::vector<bool> in_use_;
};

struct ProgressInfo {
  std::uint64_t bytes_transferred = 0;
  std::uint64_t total_bytes = 0;
  double percentage = 0;
  double speed_mbps = 0;
  std::int64_t elapsed_seconds = 0;
  std::int64_t remaining_seconds = 0;
};

using ProgressCallback = std::function<void(const ProgressInfo&)>;

class ProgressTracker {
 public:
  ProgressTracker();
  ~ProgressTracker();

  void start(std::uint64_t total_bytes);
  void update(std::uint64_t bytes_transferred);
  void finish();

  void set_callback(ProgressCallback callback);

  ProgressInfo current() const;
  bool is_complete() const;

 private:
  ProgressInfo info_;
  ProgressCallback callback_;
  std::int64_t start_time_ = 0;
  bool complete_ = false;
};

class RateLimiter {
 public:
  explicit RateLimiter(std::uint64_t bytes_per_second = 10 * 1024 * 1024);
  ~RateLimiter();

  void set_rate(std::uint64_t bytes_per_second);
  std::uint64_t rate() const;

  bool try_acquire(std::uint64_t bytes);
  void wait(std::uint64_t bytes);

 private:
  std::uint64_t rate_;
  std::uint64_t tokens_;
  std::int64_t last_refill_;
};

}  // namespace hyprshare::perf
