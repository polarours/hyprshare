#pragma once

#include <expected>
#include <filesystem>
#include <string>

#include "hyprshare/tls/tls.hpp"

namespace hyprshare::tls {

struct TlsTransportConfig {
  bool enabled = false;
  std::filesystem::path cert_path;
  std::filesystem::path key_path;
  std::filesystem::path ca_path;
  bool verify_peer = false;
};

std::expected<void, std::string> upgrade_client_socket(
  int fd,
  const TlsTransportConfig& config,
  TlsContext& ctx);

std::expected<void, std::string> upgrade_server_socket(
  int fd,
  const TlsTransportConfig& config,
  TlsContext& ctx);

std::expected<int, std::string> tls_send(
  TlsContext& ctx,
  const void* data,
  std::size_t size);

std::expected<int, std::string> tls_recv(
  TlsContext& ctx,
  void* buf,
  std::size_t size);

}  // namespace hyprshare::tls
