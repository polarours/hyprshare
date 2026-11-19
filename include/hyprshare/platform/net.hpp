#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace hyprshare::platform {

class ScopedFd {
 public:
  ScopedFd() = default;
  explicit ScopedFd(int fd) : fd_(fd) {}

  ScopedFd(const ScopedFd&) = delete;
  ScopedFd& operator=(const ScopedFd&) = delete;

  ScopedFd(ScopedFd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
  ScopedFd& operator=(ScopedFd&& other) noexcept {
    if (this != &other) {
      reset();
      fd_ = other.fd_;
      other.fd_ = -1;
    }
    return *this;
  }

  ~ScopedFd() { reset(); }

  [[nodiscard]] int get() const { return fd_; }
  [[nodiscard]] bool valid() const { return fd_ >= 0; }

  void reset(int next_fd = -1) {
    if (fd_ >= 0) {
      close(fd_);
    }
    fd_ = next_fd;
  }

 private:
  int fd_ = -1;
};

std::string normalized_host(std::string_view host);

std::expected<sockaddr_in, std::string> make_ipv4_address(std::string_view host, std::uint16_t port);

std::expected<void, std::string> set_socket_timeouts(int fd);

std::expected<void, std::string> send_all(int fd, const char* data, std::size_t size);

// Reads from a socket and keeps going when a signal interrupts the call. A
// child process that exits sends a signal to the whole process, which would
// otherwise end the read with an error.
std::ptrdiff_t recv_retry(int fd, void* buffer, std::size_t size);

std::filesystem::path expand_home_path(const std::filesystem::path& path);

std::expected<ScopedFd, std::string> connect_transport_socket(std::string_view host, std::uint16_t port);

}  // namespace hyprshare::platform
