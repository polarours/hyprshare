#include "hyprshare/perf/performance.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace hyprshare::perf {

BufferPool::BufferPool(const std::size_t buffer_size, const std::size_t pool_size)
  : buffer_size_(buffer_size) {
  pool_.reserve(pool_size);
  in_use_.reserve(pool_size);
  for (std::size_t i = 0; i < pool_size; ++i) {
    pool_.push_back(std::make_shared<std::vector<char>>(buffer_size));
    in_use_.push_back(false);
  }
}

BufferPool::~BufferPool() {}

std::shared_ptr<std::vector<char>> BufferPool::acquire() {
  for (std::size_t i = 0; i < pool_.size(); ++i) {
    if (!in_use_[i]) {
      in_use_[i] = true;
      return pool_[i];
    }
  }
  return std::make_shared<std::vector<char>>(buffer_size_);
}

void BufferPool::release(std::shared_ptr<std::vector<char>> buffer) {
  for (std::size_t i = 0; i < pool_.size(); ++i) {
    if (pool_[i] == buffer) {
      in_use_[i] = false;
      return;
    }
  }
}

std::size_t BufferPool::buffer_size() const {
  return buffer_size_;
}

std::size_t BufferPool::available() const {
  std::size_t count = 0;
  for (const auto in_use : in_use_) {
    if (!in_use) count++;
  }
  return count;
}

std::size_t BufferPool::total() const {
  return pool_.size();
}

ProgressTracker::ProgressTracker() {}

ProgressTracker::~ProgressTracker() {}

void ProgressTracker::start(const std::uint64_t total_bytes) {
  info_ = ProgressInfo{};
  info_.total_bytes = total_bytes;
  // The elapsed time is kept in milliseconds: a transfer that takes less
  // than a second would otherwise report a speed of zero.
  start_time_ = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
  complete_ = false;
}

void ProgressTracker::update(const std::uint64_t bytes_transferred) {
  info_.bytes_transferred = bytes_transferred;
  info_.percentage = (info_.total_bytes > 0)
    ? (100.0 * static_cast<double>(bytes_transferred) / static_cast<double>(info_.total_bytes))
    : 0.0;

  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
  const auto elapsed_ms = now - start_time_;
  info_.elapsed_seconds = elapsed_ms / 1000;

  const auto elapsed = static_cast<double>(elapsed_ms) / 1000.0;
  if (elapsed > 0) {
    info_.speed_mbps = static_cast<double>(bytes_transferred) / (elapsed * 1024.0 * 1024.0);
    if (info_.percentage > 0 && info_.percentage < 100) {
      info_.remaining_seconds = static_cast<std::int64_t>(
        (elapsed * (100.0 - info_.percentage)) / info_.percentage);
    }
  }

  if (callback_) {
    callback_(info_);
  }
}

void ProgressTracker::finish() {
  complete_ = true;
  if (callback_) {
    callback_(info_);
  }
}

void ProgressTracker::set_callback(ProgressCallback callback) {
  callback_ = std::move(callback);
}

ProgressInfo ProgressTracker::current() const {
  return info_;
}

bool ProgressTracker::is_complete() const {
  return complete_;
}

RateLimiter::RateLimiter(const std::uint64_t bytes_per_second)
  : rate_(bytes_per_second), tokens_(bytes_per_second), last_refill_(0) {}

RateLimiter::~RateLimiter() {}

void RateLimiter::set_rate(const std::uint64_t bytes_per_second) {
  rate_ = bytes_per_second;
}

std::uint64_t RateLimiter::rate() const {
  return rate_;
}

bool RateLimiter::try_acquire(const std::uint64_t bytes) {
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();

  // Refill proportionally to elapsed time, capped at one second's worth.
  // A single coarse reset made short waits bursty and lost fractional credit.
  if (now > last_refill_ && rate_ > 0) {
    const auto elapsed_ms = static_cast<std::uint64_t>(now - last_refill_);
    const auto replenished = static_cast<std::uint64_t>(
      static_cast<double>(rate_) * elapsed_ms / 1000.0);
    tokens_ = std::min(rate_, tokens_ + replenished);
    last_refill_ = now;
  }

  if (bytes == 0) {
    return true;
  }
  if (tokens_ >= bytes) {
    tokens_ -= bytes;
    return true;
  }
  return false;
}

void RateLimiter::wait(const std::uint64_t bytes) {
  if (bytes == 0) {
    return;
  }
  // A request larger than the whole bucket can never succeed as one acquire:
  // stream it in rate_-sized chunks instead of spinning forever.
  std::uint64_t remaining = bytes;
  while (remaining > 0) {
    const auto chunk = std::min(remaining, rate_ == 0 ? remaining : rate_);
    while (!try_acquire(chunk)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    remaining -= chunk;
  }
}

}  // namespace hyprshare::perf
