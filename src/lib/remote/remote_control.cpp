#include "hyprshare/platform/net.hpp"
#include "hyprshare/remote/remote_control.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>

namespace hyprshare::remote {

namespace {

// `std::stoi` throws on malformed coordinates, which would unwind the detached
// server's client thread and take the service down on one bad phone packet.
int32_t parse_coordinate(const std::string& line, const std::string& key) {
  const auto pos = line.find("\"" + key + "\":");
  if (pos == std::string::npos) {
    return 0;
  }
  const auto digits = line.substr(pos + key.size() + 3);
  int32_t value = 0;
  const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value);
  return result.ec == std::errc{} ? value : 0;
}

bool send_all(int fd, const void* data, std::size_t size) {
  const auto* bytes = static_cast<const char*>(data);
  std::size_t sent = 0;
  while (sent < size) {
    const auto n = send(fd, bytes + sent, size - sent, 0);
    if (n <= 0) {
      return false;
    }
    sent += static_cast<std::size_t>(n);
  }
  return true;
}

}  // namespace

RemoteControlServer::RemoteControlServer(const uint16_t port) : port_(port) {}

RemoteControlServer::~RemoteControlServer() {
  stop();
}

std::expected<void, std::string> RemoteControlServer::start() {
  server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd_ < 0) {
    return std::unexpected("failed to create socket");
  }

  int reuse = 1;
  setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port_);

  if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    close(server_fd_);
    return std::unexpected("failed to bind to port " + std::to_string(port_));
  }

  if (listen(server_fd_, 5) != 0) {
    close(server_fd_);
    return std::unexpected("failed to listen");
  }

  running_ = true;
  accept_thread_ = std::thread(&RemoteControlServer::accept_loop, this);

  return {};
}

void RemoteControlServer::stop() {
  running_ = false;
  if (server_fd_ >= 0) {
    // shutdown() wakes up the thread that is waiting in accept().
    ::shutdown(server_fd_, SHUT_RDWR);
  }
  // The thread has to be finished before the descriptor is closed, otherwise
  // the number can be handed to another socket while the thread still waits
  // on it.
  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }
  if (server_fd_ >= 0) {
    close(server_fd_);
    server_fd_ = -1;
  }
  {
    std::lock_guard<std::mutex> lock(client_mutex_);
    for (const auto fd : client_fds_) {
      if (fd >= 0) {
        close(fd);
      }
    }
    client_fds_.clear();
  }
}

bool RemoteControlServer::is_running() const {
  return running_;
}

void RemoteControlServer::set_input_callback(InputCallback callback) {
  input_callback_ = std::move(callback);
}

void RemoteControlServer::set_frame_callback(FrameCallback callback) {
  frame_callback_ = std::move(callback);
}

std::expected<void, std::string> RemoteControlServer::send_screen_frame(
  const uint32_t width,
  const uint32_t height,
  const uint8_t* data,
  const size_t size) {
  const std::string header = "{\"type\":\"screen_frame\",\"payload\":{\"width\":" +
                            std::to_string(width) + ",\"height\":" + std::to_string(height) +
                            ",\"size\":" + std::to_string(size) + "}}\n";

  std::unique_lock<std::mutex> lock(client_mutex_);
  if (client_fds_.empty()) {
    return std::unexpected("no clients connected");
  }

  int delivered = 0;
  int last_errno = 0;
  for (const auto fd : client_fds_) {
    if (fd < 0) {
      continue;
    }
    if (send_all(fd, header.data(), header.size()) &&
        (size == 0 || send_all(fd, data, size))) {
      ++delivered;
    } else {
      last_errno = errno;
    }
  }
  lock.unlock();

  if (delivered == 0) {
    std::string message = "failed to send frame to any client";
    if (last_errno != 0) {
      message += ": " + std::string(std::strerror(last_errno));
    }
    return std::unexpected(message);
  }

  // Notify observers after the lock is released: a callback may itself send a
  // frame or otherwise re-enter this server.
  if (frame_callback_ && data != nullptr) {
    ScreenFrame frame;
    frame.width = width;
    frame.height = height;
    frame.format = "rgb24";
    frame.data.assign(data, data + size);
    frame_callback_(frame);
  }
  return {};
}

void RemoteControlServer::accept_loop() {
  while (running_) {
    const auto client_fd = accept(server_fd_, nullptr, nullptr);
    if (client_fd < 0) {
      if (running_) {
        std::cerr << "remote: accept failed\n";
      }
      continue;
    }

    std::thread(&RemoteControlServer::handle_client, this, client_fd).detach();
  }
}

void RemoteControlServer::handle_client(const int client_fd) {
  {
    std::lock_guard<std::mutex> lock(client_mutex_);
    client_fds_.push_back(client_fd);
  }

  char buffer[4096];
  std::string accumulated;

  while (running_) {
    const auto received = platform::recv_retry(client_fd, buffer, sizeof(buffer) - 1);
    if (received <= 0) {
      break;
    }
    buffer[received] = '\0';
    accumulated += buffer;

    // Drain EVERY complete control line. A single recv can carry several
    // messages; handling only one left the rest stuck in `accumulated` until
    // another packet arrived, so events were silently dropped.
    for (;;) {
      const auto newline_pos = accumulated.find('\n');
      if (newline_pos == std::string::npos) {
        break;
      }
      const auto line = accumulated.substr(0, newline_pos);
      accumulated.erase(0, newline_pos + 1);

      if (line.find("\"type\":\"input_event\"") != std::string::npos) {
        InputEvent event;

        const auto type_pos = line.find("\"event_type\":\"");
        if (type_pos != std::string::npos) {
          const auto start = type_pos + 14;
          const auto end = line.find('"', start);
          if (end != std::string::npos) {
            const auto type_str = line.substr(start, end - start);
            if (type_str == "touch") {
              event.type = InputEventType::Touch;
            } else if (type_str == "keyboard") {
              event.type = InputEventType::Keyboard;
            } else if (type_str == "gesture") {
              event.type = InputEventType::Gesture;
            }
          }
        }

        event.x = parse_coordinate(line, "x");
        event.y = parse_coordinate(line, "y");

        if (input_callback_) {
          input_callback_(event);
        }
      }
    }
  }

  close(client_fd);
  {
    std::lock_guard<std::mutex> lock(client_mutex_);
    client_fds_.erase(std::remove(client_fds_.begin(), client_fds_.end(), client_fd), client_fds_.end());
  }
}

}  // namespace hyprshare::remote
