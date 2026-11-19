#include "hyprshare/tls/tls_transport.hpp"

#include <unistd.h>

namespace hyprshare::tls {

std::expected<void, std::string> upgrade_client_socket(
  const int fd,
  const TlsTransportConfig& config,
  TlsContext& ctx) {
  if (!config.enabled) {
    return {};
  }

  auto client_ctx = create_client_context();
  if (!client_ctx.has_value()) {
    return std::unexpected("failed to create TLS client context: " + client_ctx.error());
  }
  ctx = *client_ctx;

  if (!config.cert_path.empty()) {
    auto load_cert = load_certificate(ctx, config.cert_path);
    if (!load_cert.has_value()) {
      return std::unexpected("failed to load client certificate: " + load_cert.error());
    }
  }

  if (!config.key_path.empty()) {
    auto load_key = load_private_key(ctx, config.key_path);
    if (!load_key.has_value()) {
      return std::unexpected("failed to load client key: " + load_key.error());
    }
  }

  if (!config.ca_path.empty() && config.verify_peer) {
    auto load_ca = load_ca_certificate(ctx, config.ca_path);
    if (!load_ca.has_value()) {
      return std::unexpected("failed to load CA certificate: " + load_ca.error());
    }
  }

  auto set_fd_result = set_fd(ctx, fd);
  if (!set_fd_result.has_value()) {
    return std::unexpected("failed to set fd: " + set_fd_result.error());
  }

  auto state_result = set_connect_state(ctx);
  if (!state_result.has_value()) {
    return std::unexpected("failed to prepare the client handshake: " + state_result.error());
  }

  auto handshake_result = handshake(ctx);
  if (!handshake_result.has_value()) {
    return std::unexpected("TLS handshake failed: " + handshake_result.error());
  }

  return {};
}

std::expected<void, std::string> upgrade_server_socket(
  const int fd,
  const TlsTransportConfig& config,
  TlsContext& ctx) {
  if (!config.enabled) {
    return {};
  }

  auto server_ctx = create_server_context();
  if (!server_ctx.has_value()) {
    return std::unexpected("failed to create TLS server context: " + server_ctx.error());
  }
  ctx = *server_ctx;

  auto load_cert = load_certificate(ctx, config.cert_path);
  if (!load_cert.has_value()) {
    return std::unexpected("failed to load server certificate: " + load_cert.error());
  }

  auto load_key = load_private_key(ctx, config.key_path);
  if (!load_key.has_value()) {
    return std::unexpected("failed to load server key: " + load_key.error());
  }

  if (!config.ca_path.empty() && config.verify_peer) {
    auto load_ca = load_ca_certificate(ctx, config.ca_path);
    if (!load_ca.has_value()) {
      return std::unexpected("failed to load CA certificate: " + load_ca.error());
    }
  }

  auto set_fd_result = set_fd(ctx, fd);
  if (!set_fd_result.has_value()) {
    return std::unexpected("failed to set fd: " + set_fd_result.error());
  }

  auto state_result = set_accept_state(ctx);
  if (!state_result.has_value()) {
    return std::unexpected("failed to prepare the server handshake: " + state_result.error());
  }

  auto handshake_result = handshake(ctx);
  if (!handshake_result.has_value()) {
    return std::unexpected("TLS handshake failed: " + handshake_result.error());
  }

  return {};
}

std::expected<int, std::string> tls_send(
  TlsContext& ctx,
  const void* data,
  const std::size_t size) {
  if (ctx.ssl == nullptr) {
    return -1;
  }
  return tls_write(ctx, data, static_cast<int>(size));
}

std::expected<int, std::string> tls_recv(
  TlsContext& ctx,
  void* buf,
  const std::size_t size) {
  if (ctx.ssl == nullptr) {
    return -1;
  }
  return tls_read(ctx, buf, static_cast<int>(size));
}

}  // namespace hyprshare::tls
