#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>

#include "hyprshare/daemon/daemon_state.hpp"
#include "hyprshare/platform/paths.hpp"

namespace hyprshare::daemon {

class DaemonServer {
 public:
  DaemonServer(const platform::Paths& paths, const core::Config& config,
               const core::BackendRegistry& backends, const core::AdapterRegistry& adapters);
  ~DaemonServer();

  bool start();
  void stop();
  bool is_running() const;

  std::filesystem::path socket_path() const;

 private:
  void accept_loop();
  void handle_client(int client_fd);

  DaemonState state_;
  int server_fd_ = -1;
  std::atomic<bool> running_{false};
  std::thread accept_thread_;
};

}  // namespace hyprshare::daemon
