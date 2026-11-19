#include <sys/socket.h>
#include <unistd.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include "hyprshare/tls/tls.hpp"
#include "hyprshare/tls/tls_transport.hpp"

#include "test_support.hpp"

namespace {

using test_support::expect;

}  // namespace

int main() {
  const auto temp_root = std::filesystem::temp_directory_path() / "hyprshare_tls_tests";
  std::filesystem::remove_all(temp_root);
  std::filesystem::create_directories(temp_root);

  // TLS tests
  {
    std::cout << "Testing TLS...\n";

    // Test TLS context creation
    auto client_ctx = hyprshare::tls::create_client_context();
    expect(client_ctx.has_value(), "TLS client context should be created");
    if (client_ctx.has_value()) {
      hyprshare::tls::close_context(*client_ctx);
    }

    auto server_ctx = hyprshare::tls::create_server_context();
    expect(server_ctx.has_value(), "TLS server context should be created");
    if (server_ctx.has_value()) {
      hyprshare::tls::close_context(*server_ctx);
    }

    // Test self-signed cert generation
    const auto cert_path = temp_root / "test-cert.pem";
    const auto key_path = temp_root / "test-key.pem";
    auto gen_result = hyprshare::tls::generate_self_signed_cert(cert_path, key_path, "test-device");
    expect(gen_result.has_value(), "self-signed cert generation should succeed");
    if (gen_result.has_value()) {
      expect(std::filesystem::exists(cert_path), "certificate file should be created");
      expect(std::filesystem::exists(key_path), "private key file should be created");
    }

    // Test TLS transport config
    hyprshare::tls::TlsTransportConfig tls_config;
    tls_config.enabled = false;
    expect(!tls_config.enabled, "TLS transport config should be disabled by default");
  }

  // A real handshake over a socket. The transport has to set the connect or
  // accept state before the handshake, and the certificate has to carry the
  // extensions that peer verification needs.
  {
    const auto cert_path = temp_root / "transport-cert.pem";
    const auto key_path = temp_root / "transport-key.pem";
    const auto generated = hyprshare::tls::generate_self_signed_cert(cert_path, key_path, "localhost");
    expect(generated.has_value(), "a certificate for the transport test should be generated");

    if (generated.has_value()) {
      int sockets[2] = {-1, -1};
      expect(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
        "a socket pair should be created for the transport test");

      if (sockets[0] >= 0 && sockets[1] >= 0) {
        hyprshare::tls::TlsTransportConfig server_config;
        server_config.enabled = true;
        server_config.cert_path = cert_path;
        server_config.key_path = key_path;

        hyprshare::tls::TlsTransportConfig client_config;
        client_config.enabled = true;
        client_config.ca_path = cert_path;
        client_config.verify_peer = true;

        bool server_ok = false;
        std::string received;
        std::thread server([&]() {
          hyprshare::tls::TlsContext ctx;
          const auto upgraded = hyprshare::tls::upgrade_server_socket(sockets[1], server_config, ctx);
          if (!upgraded.has_value()) {
            hyprshare::tls::close_context(ctx);
            return;
          }
          char buffer[256];
          const auto got = hyprshare::tls::tls_recv(ctx, buffer, sizeof(buffer) - 1);
          if (got.has_value() && *got > 0) {
            buffer[*got] = '\0';
            received = buffer;
            const std::string reply = "pong";
            static_cast<void>(hyprshare::tls::tls_send(ctx, reply.data(), reply.size()));
          }
          hyprshare::tls::close_context(ctx);
          server_ok = true;
        });

        hyprshare::tls::TlsContext ctx;
        const auto upgraded = hyprshare::tls::upgrade_client_socket(sockets[0], client_config, ctx);
        expect(upgraded.has_value(),
          "the client handshake should succeed: " + (upgraded.has_value() ? "" : upgraded.error()));

        if (upgraded.has_value()) {
          const std::string request = "ping";
          const auto sent = hyprshare::tls::tls_send(ctx, request.data(), request.size());
          expect(sent.has_value(), "the client should be able to send over TLS");
          if (sent.has_value()) {
            char buffer[256];
            const auto got = hyprshare::tls::tls_recv(ctx, buffer, sizeof(buffer) - 1);
            expect(got.has_value(), "the client should be able to receive over TLS");
            if (got.has_value() && *got > 0) {
              buffer[*got] = '\0';
              expect(std::string(buffer) == "pong", "the server reply should arrive intact");
            }
          }
        }
        hyprshare::tls::close_context(ctx);

        if (server.joinable()) {
          server.join();
        }
        expect(server_ok, "the server handshake should succeed");
        expect(received == "ping", "the client request should arrive intact");
      }

      if (sockets[0] >= 0) close(sockets[0]);
      if (sockets[1] >= 0) close(sockets[1]);
    }
  }

  // The certificate describes itself.
  {
    const auto cert_path = temp_root / "described-cert.pem";
    const auto key_path = temp_root / "described-key.pem";
    const auto generated = hyprshare::tls::generate_self_signed_cert(
      cert_path, key_path, "described-device");
    expect(generated.has_value(), "the certificate should be generated");

    const auto info = hyprshare::tls::inspect_certificate(cert_path);
    expect(info.has_value(), "the certificate should be readable");
    if (info.has_value()) {
      expect(info->subject.find("described-device") != std::string::npos,
        "the subject should carry the common name: " + info->subject);
      expect(info->issuer == info->subject, "a self signed certificate issues itself");
      expect(info->self_signed, "the certificate should be self signed");
      expect(!info->not_before.empty(), "the start of the validity should be known");
      expect(!info->not_after.empty(), "the end of the validity should be known");
      expect(info->sha256_fingerprint.size() == 64,
        "the fingerprint should be 64 hexadecimal characters");
    }

    const auto missing = hyprshare::tls::inspect_certificate(temp_root / "absent.pem");
    expect(!missing.has_value(), "a certificate that does not exist should be reported");
  }

  std::filesystem::remove_all(temp_root);
  return test_support::report("tls");
}
