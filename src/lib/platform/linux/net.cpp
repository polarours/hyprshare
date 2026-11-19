#include "hyprshare/platform/net.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <netdb.h>
#include <string>
#include <thread>

namespace hyprshare::platform {

std::string normalized_host(const std::string_view host) {
  if (host == "localhost") {
    return "127.0.0.1";
  }
  return std::string(host);
}

std::expected<sockaddr_in, std::string> make_ipv4_address(const std::string_view host, const std::uint16_t port) {
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);

  const std::string normalized = normalized_host(host);
  if (normalized == "0.0.0.0") {
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    return address;
  }

  if (inet_pton(AF_INET, normalized.c_str(), &address.sin_addr) != 1) {
    return std::unexpected("only IPv4 literal hosts are supported right now: " + normalized);
  }
  return address;
}

std::expected<void, std::string> set_socket_timeouts(const int fd) {
  timeval timeout{};
  timeout.tv_sec = 5;
  timeout.tv_usec = 0;

  if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
    return std::unexpected("failed to configure receive timeout: " + std::string(std::strerror(errno)));
  }
  if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
    return std::unexpected("failed to configure send timeout: " + std::string(std::strerror(errno)));
  }
  return {};
}

std::ptrdiff_t recv_retry(const int fd, void* buffer, const std::size_t size) {
  while (true) {
    const ssize_t received = recv(fd, buffer, size, 0);
    if (received < 0 && errno == EINTR) {
      continue;
    }
    return received;
  }
}

std::expected<void, std::string> send_all(const int fd, const char* data, const std::size_t size) {
  std::size_t written = 0;
  while (written < size) {
    const auto sent = send(fd, data + written, size - written, MSG_NOSIGNAL);
    if (sent < 0) {
      return std::unexpected("socket send failed: " + std::string(std::strerror(errno)));
    }
    if (sent == 0) {
      return std::unexpected("socket send returned zero bytes");
    }
    written += static_cast<std::size_t>(sent);
  }
  return {};
}

std::filesystem::path expand_home_path(const std::filesystem::path& path) {
  const std::string value = path.string();
  if (value == "~") {
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
      return home;
    }
    return {};
  }
  if (value.rfind("~/", 0) == 0) {
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
      return std::filesystem::path(home) / value.substr(2);
    }
    return {};
  }
  return path;
}

std::expected<ScopedFd, std::string> connect_transport_socket(const std::string_view host, const std::uint16_t port) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo* raw_results = nullptr;
  const std::string host_text(host);
  const std::string service = std::to_string(port);
  const int resolved = getaddrinfo(host_text.c_str(), service.c_str(), &hints, &raw_results);
  if (resolved != 0) {
    return std::unexpected("failed to resolve remote host `" + host_text + "`: " + gai_strerror(resolved));
  }

  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> results(raw_results, freeaddrinfo);
  std::string last_connect_error = "no usable IPv4 address was returned";
  for (int attempt = 0; attempt < 40; ++attempt) {
    for (const addrinfo* current = results.get(); current != nullptr; current = current->ai_next) {
      ScopedFd socket_fd(socket(current->ai_family, current->ai_socktype, current->ai_protocol));
      if (!socket_fd.valid()) {
        last_connect_error = std::strerror(errno);
        continue;
      }

      auto timeout_result = set_socket_timeouts(socket_fd.get());
      if (!timeout_result.has_value()) {
        return std::unexpected(timeout_result.error());
      }

      if (connect(socket_fd.get(), current->ai_addr, current->ai_addrlen) == 0) {
        return socket_fd;
      }

      last_connect_error = std::strerror(errno);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  return std::unexpected("failed to connect to `" + host_text + ":" + service + "`: " + last_connect_error);
}

}  // namespace hyprshare::platform
