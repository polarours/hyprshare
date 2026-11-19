#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <string>

namespace hyprshare::tls {

struct TlsContext {
  void* ssl_ctx = nullptr;
  void* ssl = nullptr;
  int fd = -1;
  bool owns_fd = false;
};

std::expected<TlsContext, std::string> create_client_context();
std::expected<TlsContext, std::string> create_server_context();

std::expected<void, std::string> load_certificate(TlsContext& ctx, const std::filesystem::path& cert_path);
std::expected<void, std::string> load_private_key(TlsContext& ctx, const std::filesystem::path& key_path);
std::expected<void, std::string> load_ca_certificate(TlsContext& ctx, const std::filesystem::path& ca_path);

std::expected<void, std::string> set_fd(TlsContext& ctx, int fd);

// Prepares the session for the client or the server side of the handshake.
// OpenSSL refuses to handshake until one of these is set.
std::expected<void, std::string> set_connect_state(TlsContext& ctx);
std::expected<void, std::string> set_accept_state(TlsContext& ctx);

std::expected<void, std::string> handshake(TlsContext& ctx);

// Verifies the peer certificate against the system trust store.
std::expected<void, std::string> enable_peer_verification(TlsContext& ctx);

// Requires the peer certificate to match the given host name.
std::expected<void, std::string> set_peer_hostname(TlsContext& ctx, const std::string& host);

std::expected<int, std::string> tls_read(TlsContext& ctx, void* buf, int num);
std::expected<int, std::string> tls_write(TlsContext& ctx, const void* buf, int num);

std::expected<std::string, std::string> get_peer_cert_fingerprint(TlsContext& ctx);

// What a certificate says about itself.
struct CertificateInfo {
  std::string subject;
  std::string issuer;
  std::string not_before;
  std::string not_after;
  std::string sha256_fingerprint;
  std::int64_t serial = 0;
  bool self_signed = false;
};

// Reads a certificate from a file and describes it.
std::expected<CertificateInfo, std::string> inspect_certificate(
  const std::filesystem::path& cert_path);
std::expected<std::string, std::string> get_local_cert_fingerprint(TlsContext& ctx);

void close_context(TlsContext& ctx);

std::expected<void, std::string> generate_self_signed_cert(
  const std::filesystem::path& cert_path,
  const std::filesystem::path& key_path,
  const std::string& common_name);

}  // namespace hyprshare::tls
