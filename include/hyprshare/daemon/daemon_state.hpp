#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "hyprshare/core/backend_registry.hpp"
#include "hyprshare/core/config.hpp"
#include "hyprshare/core/adapter_registry.hpp"
#include "hyprshare/platform/paths.hpp"
#include "hyprshare/platform/process.hpp"

namespace hyprshare::daemon {

struct ListenerInfo {
  int fd = -1;
  std::string host;
  uint16_t port = 0;
  std::filesystem::path destination;
  bool running = false;
};

class DaemonState {
 public:
  DaemonState(const platform::Paths& paths, const core::Config& config,
              const core::BackendRegistry& backends, const core::AdapterRegistry& adapters);

  bool start_listener(const std::string& host, uint16_t port,
                      const std::filesystem::path& destination,
                      uint32_t count, uint32_t timeout, uint64_t max_bytes,
                      const std::string& token, bool announce);
  bool stop_listener(int listener_fd);
  std::vector<ListenerInfo> list_listeners() const;

  void shutdown();

  const platform::Paths& paths() const { return paths_; }
  const core::Config& config() const { return config_; }

 private:
  platform::Paths paths_;
  core::Config config_;
  core::BackendRegistry backends_;
  core::AdapterRegistry adapters_;
  platform::ProcessRunner runner_;
  mutable std::mutex mutex_;
  struct InternalListener {
    // The identifier the caller uses to stop the listener.
    int id = -1;
    std::string host;
    uint16_t port = 0;
    std::filesystem::path destination;
    std::thread thread;
    // Whether the listener is active. The stop request below has the
    // opposite meaning: the inbox server reads it as "stop when true".
    std::atomic<bool> running{false};
    std::atomic<bool> stop_requested{false};
  };
  std::vector<std::unique_ptr<InternalListener>> listeners_;
  int next_listener_id_ = 1;
};

}  // namespace hyprshare::daemon
