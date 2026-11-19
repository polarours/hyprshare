#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "hyprshare/http/http_client.hpp"
#include "hyprshare/tls/tls.hpp"
#include "hyprshare/tls/tls_transport.hpp"

#include "test_support.hpp"

namespace {

using test_support::expect;

// A minimal HTTP server that answers a handful of fixed routes. It runs on a
// background thread for the lifetime of the test.
class TestServer {
 public:
  TestServer() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
      return;
    }

    int reuse = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listen_fd_, 8) != 0) {
      close(listen_fd_);
      listen_fd_ = -1;
      return;
    }

    sockaddr_in bound{};
    socklen_t length = sizeof(bound);
    getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&bound), &length);
    port_ = ntohs(bound.sin_port);

    thread_ = std::thread([this]() { serve(); });
  }

  ~TestServer() {
    stop();
  }

  bool valid() const { return listen_fd_ >= 0 && port_ != 0; }
  std::uint16_t port() const { return port_; }

  void stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
      // shutdown() wakes up a thread that is blocked in accept(); closing the
      // descriptor alone would not.
      ::shutdown(listen_fd_, SHUT_RDWR);
    }
    if (thread_.joinable()) {
      thread_.join();
    }
    if (listen_fd_ >= 0) {
      close(listen_fd_);
      listen_fd_ = -1;
    }
  }

 private:
  void serve() {
    while (running_) {
      const int client = accept(listen_fd_, nullptr, nullptr);
      if (client < 0) {
        if (!running_) {
          break;
        }
        continue;
      }
      handle(client);
      close(client);
    }
  }

  void handle(const int client) {
    std::string request;
    char buffer[4096];
    while (request.find("\r\n\r\n") == std::string::npos) {
      const ssize_t received = recv(client, buffer, sizeof(buffer), 0);
      if (received <= 0) {
        return;
      }
      request.append(buffer, static_cast<std::size_t>(received));
    }

    const auto header_end = request.find("\r\n\r\n");
    const std::string head = request.substr(0, header_end);
    std::string body = request.substr(header_end + 4);

    // Read the announced body, if any.
    const auto length_pos = head.find("Content-Length:");
    if (length_pos != std::string::npos) {
      const std::size_t expected = std::stoul(head.substr(length_pos + 15));
      while (body.size() < expected) {
        const ssize_t received = recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0) {
          break;
        }
        body.append(buffer, static_cast<std::size_t>(received));
      }
    }

    last_request_ = head;
    last_body_ = body;

    std::string response;
    if (head.rfind("GET /hello", 0) == 0) {
      response = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nX-Test: yes\r\nConnection: close\r\n\r\nhello";
    } else if (head.rfind("GET /chunked", 0) == 0) {
      response = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
                 "5\r\nchunk\r\n3\r\ned!\r\n0\r\n\r\n";
    } else if (head.rfind("GET /missing", 0) == 0) {
      response = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\nConnection: close\r\n\r\nnot found";
    } else if (head.rfind("PUT /echo", 0) == 0) {
      response = "HTTP/1.1 201 Created\r\nContent-Length: " + std::to_string(body.size()) +
                 "\r\nConnection: close\r\n\r\n" + body;
    } else {
      response = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    }

    send(client, response.data(), response.size(), MSG_NOSIGNAL);
  }

 public:
  std::string last_request() const { return last_request_; }
  std::string last_body() const { return last_body_; }

 private:
  int listen_fd_ = -1;
  std::uint16_t port_ = 0;
  std::atomic<bool> running_{true};
  std::thread thread_;
  std::string last_request_;
  std::string last_body_;
};

}  // namespace

int main() {
  // URL parsing
  {
    const auto url = hyprshare::http::parse_url("https://example.com/path/to?x=1");
    expect(url.has_value(), "a well formed URL should parse");
    if (url.has_value()) {
      expect(url->scheme == "https", "the scheme should be parsed");
      expect(url->host == "example.com", "the host should be parsed");
      expect(url->port == 443, "https should default to port 443");
      expect(url->secure, "https should be marked secure");
      expect(url->path == "/path/to?x=1", "the path should be parsed");
    }

    const auto with_port = hyprshare::http::parse_url("http://127.0.0.1:8080/base/");
    expect(with_port.has_value(), "a URL with a port should parse");
    if (with_port.has_value()) {
      expect(with_port->port == 8080, "the port should be parsed");
      expect(!with_port->secure, "http should not be marked secure");
      expect(with_port->path == "/base/", "the path should be parsed");
    }

    const auto no_path = hyprshare::http::parse_url("http://example.com");
    expect(no_path.has_value(), "a URL without a path should parse");
    if (no_path.has_value()) {
      expect(no_path->path == "/", "a missing path should default to /");
      expect(no_path->port == 80, "http should default to port 80");
    }

    expect(!hyprshare::http::parse_url("ftp://example.com").has_value(),
      "an unsupported scheme should be rejected");
    expect(!hyprshare::http::parse_url("example.com").has_value(),
      "a URL without a scheme should be rejected");
  }

  // Joining and encoding
  {
    expect(hyprshare::http::join_url("http://example.com/", "/a") == "http://example.com/a",
      "join_url should collapse the slash between base and path");
    expect(hyprshare::http::join_url("http://example.com", "a") == "http://example.com/a",
      "join_url should insert a missing slash");
    expect(hyprshare::http::url_encode("a b/c") == "a%20b/c",
      "url_encode should escape spaces but keep path separators");
  }

  TestServer server;
  expect(server.valid(), "the test server should start");
  if (!server.valid()) {
    return test_support::report("http");
  }

  hyprshare::http::Client client;
  const std::string base = "http://127.0.0.1:" + std::to_string(server.port());

  // GET
  {
    const auto response = client.get(base + "/hello");
    expect(response.has_value(), "a GET request should succeed");
    if (response.has_value()) {
      expect(response->status == 200, "the status code should be parsed");
      expect(response->reason == "OK", "the reason phrase should be parsed");
      expect(response->body == "hello", "the body should be read");
      const auto header = response->headers.find("x-test");
      expect(header != response->headers.end(), "response headers should be parsed");
      if (header != response->headers.end()) {
        expect(header->second == "yes", "the header value should be read");
      }
    }
  }

  // Chunked transfer encoding
  {
    const auto response = client.get(base + "/chunked");
    expect(response.has_value(), "a chunked response should succeed");
    if (response.has_value()) {
      expect(response->body == "chunked!", "a chunked body should be decoded: " + response->body);
    }
  }

  // PUT with a body
  {
    const auto response = client.put(base + "/echo", "payload", "text/plain");
    expect(response.has_value(), "a PUT request should succeed");
    if (response.has_value()) {
      expect(response->status == 201, "the status code should be parsed");
      expect(response->body == "payload", "the server should receive the body");
    }
    expect(server.last_body() == "payload", "the request body should reach the server");
    expect(server.last_request().find("Content-Type: text/plain") != std::string::npos,
      "the content type should be sent");
  }

  // Authorization header
  {
    hyprshare::http::RequestOptions options;
    options.bearer_token = "secret-token";
    const auto response = client.get(base + "/hello", options);
    expect(response.has_value(), "an authorized GET should succeed");
    expect(server.last_request().find("Authorization: Bearer secret-token") != std::string::npos,
      "the bearer token should be sent");
  }

  // Error status codes are reported, not swallowed
  {
    const auto response = client.get(base + "/missing");
    expect(response.has_value(), "a 404 response is still a response");
    if (response.has_value()) {
      expect(response->status == 404, "the status code should be 404");
    }
  }

  // Connection failures are reported
  {
    const auto response = client.get("http://127.0.0.1:1/nothing");
    expect(!response.has_value(), "connecting to a closed port should fail");
  }

  // Malformed URLs are rejected before any connection is attempted
  {
    expect(!client.get("not-a-url").has_value(), "a malformed URL should be rejected");
  }

  server.stop();

  // TLS: the client has to verify the certificate against the trust store it
  // was given, and it has to reject a certificate that does not match the
  // host it connected to.
  {
    const auto temp_root = std::filesystem::temp_directory_path() / "hyprshare_http_tests";
    std::filesystem::remove_all(temp_root);
    std::filesystem::create_directories(temp_root);

    const auto cert_path = temp_root / "server-cert.pem";
    const auto key_path = temp_root / "server-key.pem";
    const auto generated = hyprshare::tls::generate_self_signed_cert(cert_path, key_path, "localhost");
    expect(generated.has_value(), "a self-signed certificate should be generated");
    if (!generated.has_value()) {
      return test_support::report("http");
    }

    const int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    expect(listen_fd >= 0, "the TLS test server socket should be created");
    if (listen_fd < 0) {
      return test_support::report("http");
    }

    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    bind(listen_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    listen(listen_fd, 4);
    sockaddr_in bound{};
    socklen_t length = sizeof(bound);
    getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bound), &length);
    const auto tls_port = ntohs(bound.sin_port);

    std::atomic<bool> tls_running{true};
    std::thread tls_thread([&]() {
      while (tls_running) {
        const int accepted = accept(listen_fd, nullptr, nullptr);
        if (accepted < 0) {
          break;
        }

        hyprshare::tls::TlsTransportConfig config;
        config.enabled = true;
        config.cert_path = cert_path;
        config.key_path = key_path;

        hyprshare::tls::TlsContext ctx;
        const auto upgraded = hyprshare::tls::upgrade_server_socket(accepted, config, ctx);
        if (upgraded.has_value()) {
          char buffer[4096];
          const auto received = hyprshare::tls::tls_recv(ctx, buffer, sizeof(buffer) - 1);
          if (received.has_value() && *received > 0) {
            buffer[*received] = '\0';
            const std::string response =
              "HTTP/1.1 200 OK\r\nContent-Length: 8\r\nConnection: close\r\n\r\nsecured";
            static_cast<void>(hyprshare::tls::tls_send(ctx, response.data(), response.size()));
          }
        }
        hyprshare::tls::close_context(ctx);
        close(accepted);
      }
    });

    hyprshare::http::RequestOptions options;
    options.ca_path = cert_path;
    options.verify_peer = true;

    hyprshare::http::Client tls_client;
    const auto response = tls_client.get(
      "https://localhost:" + std::to_string(tls_port) + "/hello", options);
    expect(response.has_value(), "a TLS request should succeed: " +
      (response.has_value() ? "" : response.error()));
    if (response.has_value()) {
      expect(response->status == 200, "the TLS response status should be parsed");
      expect(response->body == "secured", "the TLS response body should be read");
    }

    // Without the trust store the certificate must not be accepted.
    hyprshare::http::RequestOptions untrusted;
    untrusted.verify_peer = true;
    const auto rejected = tls_client.get(
      "https://localhost:" + std::to_string(tls_port) + "/hello", untrusted);
    expect(!rejected.has_value(), "an untrusted certificate should be rejected");

    tls_running = false;
    ::shutdown(listen_fd, SHUT_RDWR);
    if (tls_thread.joinable()) {
      tls_thread.join();
    }
    close(listen_fd);
    std::filesystem::remove_all(temp_root);
  }

  return test_support::report("http");
}
