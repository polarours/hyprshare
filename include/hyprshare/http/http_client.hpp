#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace hyprshare::http {

// A parsed URL. Only the parts the client needs are kept.
struct Url {
  std::string scheme;
  std::string host;
  std::uint16_t port = 0;
  std::string path = "/";
  bool secure = false;
};

std::expected<Url, std::string> parse_url(std::string_view url);

struct Response {
  int status = 0;
  std::string reason;
  std::map<std::string, std::string> headers;
  std::string body;
};

struct RequestOptions {
  // Sent as `Authorization: Bearer <token>`.
  std::string bearer_token;
  // Sent as `Authorization: Basic <base64(user:pass)>`.
  std::string basic_auth;
  std::chrono::seconds timeout{30};
  // Verifies the peer certificate against the system trust store.
  bool verify_peer = true;
  // Overrides the trust store used for verification.
  std::filesystem::path ca_path;
  std::string user_agent = "hyprshare";
};

// A small HTTP/1.1 client built on the OpenSSL transport that is already part
// of the project. It speaks enough of the protocol to talk to a release
// server or an object store: GET, HEAD, PUT, and DELETE over plain HTTP or
// TLS.
class Client {
 public:
  Client() = default;

  std::expected<Response, std::string> get(const std::string& url, const RequestOptions& options = {});
  std::expected<Response, std::string> head(const std::string& url, const RequestOptions& options = {});
  std::expected<Response, std::string> post(const std::string& url,
    const std::string& body,
    const std::string& content_type,
    const RequestOptions& options = {});
  std::expected<Response, std::string> put(const std::string& url,
    const std::string& body,
    const std::string& content_type,
    const RequestOptions& options = {});
  std::expected<Response, std::string> put_file(const std::string& url,
    const std::filesystem::path& file,
    const RequestOptions& options = {});
  std::expected<Response, std::string> remove(const std::string& url, const RequestOptions& options = {});

 private:
  std::expected<Response, std::string> request(const std::string& method,
    const Url& url,
    const std::string& body,
    const std::string& content_type,
    const RequestOptions& options);
};

// Joins a base URL and a path with exactly one slash between them.
std::string join_url(const std::string& base, const std::string& path);

// Percent-encodes a path segment so that it can be used in a URL.
std::string url_encode(const std::string& value);

}  // namespace hyprshare::http
