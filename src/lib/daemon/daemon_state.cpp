#include "hyprshare/daemon/daemon_state.hpp"

#include <chrono>
#include <iostream>
#include <thread>

#include "hyprshare/core/inbox.hpp"
#include "hyprshare/core/inbox_history.hpp"

namespace hyprshare::daemon {

namespace {

}  // namespace

DaemonState::DaemonState(
  const platform::Paths& paths,
  const core::Config& config,
  const core::BackendRegistry& backends,
  const core::AdapterRegistry& adapters)
  : paths_(paths), config_(config), backends_(backends), adapters_(adapters) {}

bool DaemonState::start_listener(
  const std::string& host,
  const uint16_t port,
  const std::filesystem::path& destination,
  const uint32_t,
  const uint32_t,
  const uint64_t max_bytes,
  const std::string& token,
  const bool) {
  std::lock_guard lock(mutex_);

  for (const auto& listener : listeners_) {
    if (listener->running && listener->host == host && listener->port == port) {
      std::cerr << "daemon: a listener is already running on " << host << ":" << port << "\n";
      return false;
    }
  }

  auto info = std::make_unique<InternalListener>();
  info->host = host;
  info->port = port;
  info->destination = destination;
  info->running = true;
  info->stop_requested = false;
  info->id = next_listener_id_++;

  // The listener serves the inbox of this machine. The inbox API takes over
  // the port for a while and then hands it back, so the listener binds it again
  // and keeps going until it is stopped.
  InternalListener* listener = info.get();
  info->thread = std::thread([this, listener, max_bytes, token]() {
    while (listener->running.load()) {
      // The stop flag of the listener ends the server, so the port is held
      // for as long as the listener runs and never handed out in between.
      const auto received = core::serve_inbox(
        listener->host,
        listener->port,
        listener->destination,
        0,
        token,
        0,
        max_bytes,
        &runner_,
        {},
        &listener->stop_requested);

      if (!received.has_value()) {
        if (listener->stop_requested.load()) {
          break;
        }
        // A client that connects and goes away must not take the listener
        // down with it.
        std::cerr << "daemon: the listener on " << listener->host << ":" << listener->port
                  << " hit an error: " << received.error() << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        continue;
      }
      for (const auto& file : *received) {
        static_cast<void>(core::append_inbox_history(file, paths_));
      }
    }
  });

  listeners_.push_back(std::move(info));
  std::cout << "daemon: listening on " << host << ":" << port << std::endl;
  return true;
}

bool DaemonState::stop_listener(const int listener_id) {
  std::lock_guard lock(mutex_);
  for (auto& listener : listeners_) {
    if (listener->id == listener_id) {
      listener->running = false;
      listener->stop_requested = true;
      return true;
    }
  }
  return false;
}

std::vector<ListenerInfo> DaemonState::list_listeners() const {
  std::lock_guard lock(mutex_);
  std::vector<ListenerInfo> result;
  for (const auto& listener : listeners_) {
    ListenerInfo info;
    info.fd = listener->id;
    info.host = listener->host;
    info.port = listener->port;
    info.destination = listener->destination;
    info.running = listener->running.load();
    result.push_back(info);
  }
  return result;
}

void DaemonState::shutdown() {
  std::vector<std::thread> threads;
  {
    std::lock_guard lock(mutex_);
    for (auto& listener : listeners_) {
      listener->running = false;
      listener->stop_requested = true;
      if (listener->thread.joinable()) {
        threads.push_back(std::move(listener->thread));
      }
    }
    listeners_.clear();
  }

  // The threads are joined outside the lock, because they take the lock
  // themselves when they record a received file.
  for (auto& thread : threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }
}

}  // namespace hyprshare::daemon
