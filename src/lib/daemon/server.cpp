#include "hyprshare/daemon/server.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>

#include "hyprshare/daemon/ipc.hpp"
#include "hyprshare/platform/net.hpp"

namespace hyprshare::daemon {

DaemonServer::DaemonServer(
  const platform::Paths& paths,
  const core::Config& config,
  const core::BackendRegistry& backends,
  const core::AdapterRegistry& adapters)
  : state_(paths, config, backends, adapters) {}

DaemonServer::~DaemonServer() {
  stop();
}

std::filesystem::path DaemonServer::socket_path() const {
  // The runtime directory already carries the name of the application.
  return state_.paths().runtime_dir / "hyprshared.sock";
}

bool DaemonServer::start() {
  const auto path = socket_path();

  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    std::cerr << "daemon: failed to create socket directory: " << error.message() << "\n";
    return false;
  }

  if (std::filesystem::exists(path)) {
    std::filesystem::remove(path, error);
    if (error) {
      std::cerr << "daemon: failed to remove stale socket: " << error.message() << "\n";
      return false;
    }
  }

  server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
  if (server_fd_ < 0) {
    std::cerr << "daemon: failed to create socket: " << std::strerror(errno) << "\n";
    return false;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  const auto path_str = path.string();
  if (path_str.size() >= sizeof(addr.sun_path)) {
    std::cerr << "daemon: socket path too long\n";
    close(server_fd_);
    server_fd_ = -1;
    return false;
  }
  std::strncpy(addr.sun_path, path_str.c_str(), sizeof(addr.sun_path) - 1);

  if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    std::cerr << "daemon: failed to bind socket: " << std::strerror(errno) << "\n";
    close(server_fd_);
    server_fd_ = -1;
    return false;
  }

  if (listen(server_fd_, 5) != 0) {
    std::cerr << "daemon: failed to listen: " << std::strerror(errno) << "\n";
    close(server_fd_);
    server_fd_ = -1;
    return false;
  }

  running_ = true;
  accept_thread_ = std::thread(&DaemonServer::accept_loop, this);

  std::cout << "daemon: listening on " << path_str << "\n";
  return true;
}

void DaemonServer::stop() {
  running_ = false;
  state_.shutdown();

  if (server_fd_ >= 0) {
    close(server_fd_);
    server_fd_ = -1;
  }

  const auto path = socket_path();
  std::error_code error;
  std::filesystem::remove(path, error);

  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }
}

bool DaemonServer::is_running() const {
  return running_;
}

void DaemonServer::accept_loop() {
  while (running_) {
    const auto client_fd = accept(server_fd_, nullptr, nullptr);
    if (client_fd < 0) {
      if (running_) {
        std::cerr << "daemon: accept failed: " << std::strerror(errno) << "\n";
      }
      continue;
    }

    std::thread(&DaemonServer::handle_client, this, client_fd).detach();
  }
}

void DaemonServer::handle_client(const int client_fd) {
  std::string buffer;
  buffer.reserve(4096);
  char chunk[4096];

  while (true) {
    const auto received = platform::recv_retry(client_fd, chunk, sizeof(chunk) - 1);
    if (received <= 0) {
      break;
    }
    chunk[received] = '\0';
    buffer += chunk;

    if (buffer.find('\n') != std::string::npos) {
      break;
    }
  }

  if (buffer.empty()) {
    close(client_fd);
    return;
  }

  const auto request = parse_request(buffer);
  IpcResponse response;

  if (!request.has_value()) {
    response.ok = false;
    response.error = request.error();
  } else {
    if (request->command == "inbox.start_listener") {
      const auto host = request->args.count("host") ? request->args.at("host") : "0.0.0.0";
      const auto port_str = request->args.count("port") ? request->args.at("port") : "28556";
      const auto port = static_cast<uint16_t>(std::stoul(port_str));
      const auto destination = request->args.count("destination")
        ? std::filesystem::path(request->args.at("destination"))
        : std::filesystem::path(state_.config().files.receive_dir);
      const auto count_str = request->args.count("count") ? request->args.at("count") : "1";
      const auto count = static_cast<uint32_t>(std::stoul(count_str));
      const auto timeout_str = request->args.count("timeout") ? request->args.at("timeout") : "120";
      const auto timeout = static_cast<uint32_t>(std::stoul(timeout_str));
      const auto max_bytes_str = request->args.count("max_bytes") ? request->args.at("max_bytes") : "0";
      const auto max_bytes = static_cast<uint64_t>(std::stoull(max_bytes_str));
      const auto token = request->args.count("token") ? request->args.at("token") : "";
      const auto announce = request->args.count("announce") && request->args.at("announce") == "true";

      if (state_.start_listener(host, port, destination, count, timeout, max_bytes, token, announce)) {
        response.ok = true;
        response.result["status"] = "started";
      } else {
        response.ok = false;
        response.error = "failed to start listener";
      }
    } else if (request->command == "inbox.stop_listener") {
      const auto fd_str = request->args.count("fd") ? request->args.at("fd") : "-1";
      const auto fd = std::stoi(fd_str);
      if (state_.stop_listener(fd)) {
        response.ok = true;
        response.result["status"] = "stopped";
      } else {
        response.ok = false;
        response.error = "listener not found";
      }
    } else if (request->command == "inbox.list_listeners") {
      const auto listeners = state_.list_listeners();
      response.ok = true;
      response.result["count"] = std::to_string(listeners.size());

      std::string details = "[";
      for (std::size_t index = 0; index < listeners.size(); ++index) {
        if (index > 0) {
          details += ",";
        }
        details += "{\"id\":" + std::to_string(listeners[index].fd);
        details += ",\"host\":\"" + listeners[index].host + "\"";
        details += ",\"port\":" + std::to_string(listeners[index].port);
        details += ",\"destination\":\"" + listeners[index].destination.string() + "\"";
        details += ",\"running\":" + std::string(listeners[index].running ? "true" : "false");
        details += "}";
      }
      details += "]";
      response.result["listeners"] = details;
    } else if (request->command == "daemon.ping") {
      response.ok = true;
      response.result["status"] = "pong";
    } else if (request->command == "daemon.stop") {
      response.ok = true;
      response.result["status"] = "stopping";
      running_ = false;
    } else {
      response.ok = false;
      response.error = "unknown command: " + request->command;
    }
  }

  const auto response_str = serialize_response(response);
  send(client_fd, response_str.data(), response_str.size(), MSG_NOSIGNAL);
  close(client_fd);
}

}  // namespace hyprshare::daemon
