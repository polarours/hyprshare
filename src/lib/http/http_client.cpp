#include "hyprshare/http/http_client.hpp"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>

#include "hyprshare/platform/net.hpp"
#include "hyprshare/tls/tls.hpp"
#include "hyprshare/tls/tls_transport.hpp"
#include "hyprshare/util/string.hpp"

namespace hyprshare::http {

namespace {

constexpr std::size_t kMaxResponseBytes = 64u * 1024u * 1024u;

std::string to_lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
    [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string trim_copy(const std::string& value) {
  const auto start = value.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(start, end - start + 1);
}

// A connection that transparently switches between plain sockets and TLS.
class Connection {
 public:
  static std::expected<Connection, std::string> open(const Url& url, const RequestOptions& options) {
    Connection connection;
    auto socket = platform::connect_transport_socket(url.host, url.port);
    if (!socket.has_value()) {
      return std::unexpected(socket.error());
    }
    connection.socket_ = std::move(*socket);

    if (!url.secure) {
      return connection;
    }

    auto client_context = tls::create_client_context();
    if (!client_context.has_value()) {
      return std::unexpected("failed to create TLS client context: " + client_context.error());
    }
    connection.tls_ = *client_context;

    if (options.ca_path.empty()) {
      auto verification = tls::enable_peer_verification(connection.tls_);
      if (!verification.has_value()) {
        return std::unexpected(verification.error());
      }
    } else {
      auto ca = tls::load_ca_certificate(connection.tls_, options.ca_path);
      if (!ca.has_value()) {
        return std::unexpected(ca.error());
      }
    }
    auto fd = tls::set_fd(connection.tls_, connection.socket_.get());
    if (!fd.has_value()) {
      return std::unexpected(fd.error());
    }
    auto state = tls::set_connect_state(connection.tls_);
    if (!state.has_value()) {
      return std::unexpected(state.error());
    }
    // The host name has to be set on the session before the handshake, so
    // that the certificate can be checked against it.
    if (options.verify_peer) {
      auto hostname = tls::set_peer_hostname(connection.tls_, url.host);
      if (!hostname.has_value()) {
        return std::unexpected(hostname.error());
      }
    }
    auto handshake = tls::handshake(connection.tls_);
    if (!handshake.has_value()) {
      return std::unexpected(handshake.error());
    }
    return connection;
  }

  std::expected<std::size_t, std::string> write(const char* data, const std::size_t size) {
    if (tls_.ssl != nullptr) {
      auto sent = tls::tls_write(tls_, data, static_cast<int>(size));
      if (!sent.has_value()) {
        return std::unexpected(sent.error());
      }
      return static_cast<std::size_t>(*sent);
    }
    const ssize_t sent = ::send(socket_.get(), data, size, MSG_NOSIGNAL);
    if (sent < 0) {
      return std::unexpected("failed to send request: " + std::string(std::strerror(errno)));
    }
    return static_cast<std::size_t>(sent);
  }

  std::expected<std::size_t, std::string> read(char* buffer, const std::size_t size) {
    if (tls_.ssl != nullptr) {
      auto received = tls::tls_recv(tls_, buffer, static_cast<int>(size));
      if (!received.has_value()) {
        return std::unexpected(received.error());
      }
      return static_cast<std::size_t>(*received);
    }
    const ssize_t received = platform::recv_retry(socket_.get(), buffer, size);
    if (received < 0) {
      return std::unexpected("failed to read response: " + std::string(std::strerror(errno)));
    }
    return static_cast<std::size_t>(received);
  }

 private:
  platform::ScopedFd socket_;
  tls::TlsContext tls_{};
};

std::expected<Response, std::string> parse_response(const std::string& raw) {
  Response response;

  const auto header_end = raw.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    return std::unexpected("malformed response: no header terminator");
  }

  const std::string headers = raw.substr(0, header_end);
  response.body = raw.substr(header_end + 4);

  std::istringstream stream(headers);
  std::string status_line;
  if (!std::getline(stream, status_line)) {
    return std::unexpected("malformed response: empty status line");
  }

  const auto first_space = status_line.find(' ');
  if (first_space == std::string::npos) {
    return std::unexpected("malformed status line: " + status_line);
  }
  const auto second_space = status_line.find(' ', first_space + 1);
  const std::string status_text = status_line.substr(
    first_space + 1, second_space == std::string::npos ? std::string::npos : second_space - first_space - 1);
  if (second_space != std::string::npos) {
    response.reason = trim_copy(status_line.substr(second_space + 1));
  }

  try {
    response.status = std::stoi(status_text);
  } catch (const std::exception&) {
    return std::unexpected("malformed status code: " + status_text);
  }

  std::string line;
  while (std::getline(stream, line)) {
    const auto separator = line.find(':');
    if (separator == std::string::npos) {
      continue;
    }
    const std::string name = to_lower_copy(trim_copy(line.substr(0, separator)));
    const std::string value = trim_copy(line.substr(separator + 1));
    response.headers[name] = value;
  }

  // A chunked body is decoded here so that callers always see plain bytes.
  const auto transfer_encoding = response.headers.find("transfer-encoding");
  if (transfer_encoding != response.headers.end() &&
      to_lower_copy(transfer_encoding->second).find("chunked") != std::string::npos) {
    std::string decoded;
    std::size_t offset = 0;
    const std::string& body = response.body;
    while (offset < body.size()) {
      const auto line_end = body.find("\r\n", offset);
      if (line_end == std::string::npos) {
        break;
      }
      const std::string size_line = body.substr(offset, line_end - offset);
      std::size_t chunk_size = 0;
      try {
        chunk_size = std::stoul(size_line, nullptr, 16);
      } catch (const std::exception&) {
        return std::unexpected("malformed chunk size: " + size_line);
      }
      offset = line_end + 2;
      if (chunk_size == 0) {
        break;
      }
      if (offset + chunk_size > body.size()) {
        return std::unexpected("truncated chunked body");
      }
      decoded.append(body, offset, chunk_size);
      offset += chunk_size + 2;
    }
    response.body = decoded;
  }

  return response;
}

std::string base64_encode(const std::string& input) {
  static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string encoded;
  encoded.reserve(((input.size() + 2) / 3) * 4);

  std::size_t index = 0;
  while (index + 2 < input.size()) {
    const unsigned int triple = (static_cast<unsigned char>(input[index]) << 16) |
                                (static_cast<unsigned char>(input[index + 1]) << 8) |
                                static_cast<unsigned char>(input[index + 2]);
    encoded += table[(triple >> 18) & 0x3F];
    encoded += table[(triple >> 12) & 0x3F];
    encoded += table[(triple >> 6) & 0x3F];
    encoded += table[triple & 0x3F];
    index += 3;
  }

  if (index < input.size()) {
    const std::size_t remaining = input.size() - index;
    unsigned int triple = static_cast<unsigned char>(input[index]) << 16;
    if (remaining == 2) {
      triple |= static_cast<unsigned char>(input[index + 1]) << 8;
    }
    encoded += table[(triple >> 18) & 0x3F];
    encoded += table[(triple >> 12) & 0x3F];
    encoded += remaining == 2 ? table[(triple >> 6) & 0x3F] : '=';
    encoded += '=';
  }

  return encoded;
}

}  // namespace

std::expected<Url, std::string> parse_url(const std::string_view url) {
  const std::string text(url);
  const auto scheme_end = text.find("://");
  if (scheme_end == std::string::npos) {
    return std::unexpected("malformed URL, expected scheme://host: " + text);
  }

  Url parsed;
  parsed.scheme = to_lower_copy(text.substr(0, scheme_end));
  if (parsed.scheme == "https") {
    parsed.secure = true;
    parsed.port = 443;
  } else if (parsed.scheme == "http") {
    parsed.secure = false;
    parsed.port = 80;
  } else {
    return std::unexpected("unsupported URL scheme: " + parsed.scheme);
  }

  const std::string remainder = text.substr(scheme_end + 3);
  const auto path_start = remainder.find('/');
  std::string authority = remainder;
  if (path_start != std::string::npos) {
    authority = remainder.substr(0, path_start);
    parsed.path = remainder.substr(path_start);
  }
  if (authority.empty()) {
    return std::unexpected("malformed URL, missing host: " + text);
  }

  // Strip user information, keeping only the host and port.
  const auto at_sign = authority.rfind('@');
  if (at_sign != std::string::npos) {
    authority = authority.substr(at_sign + 1);
  }

  const auto colon = authority.rfind(':');
  if (colon != std::string::npos && authority.find(']') == std::string::npos) {
    const std::string port_text = authority.substr(colon + 1);
    try {
      const long port = std::stol(port_text);
      if (port <= 0 || port > 65535) {
        return std::unexpected("malformed URL, port out of range: " + port_text);
      }
      parsed.port = static_cast<std::uint16_t>(port);
    } catch (const std::exception&) {
      return std::unexpected("malformed URL, invalid port: " + port_text);
    }
    parsed.host = authority.substr(0, colon);
  } else {
    parsed.host = authority;
  }

  if (parsed.host.empty()) {
    return std::unexpected("malformed URL, missing host: " + text);
  }
  if (parsed.path.empty()) {
    parsed.path = "/";
  }

  return parsed;
}

std::string join_url(const std::string& base, const std::string& path) {
  if (base.empty()) {
    return path;
  }
  if (path.empty()) {
    return base;
  }
  if (base.back() == '/' && path.front() == '/') {
    return base + path.substr(1);
  }
  if (base.back() != '/' && path.front() != '/') {
    return base + "/" + path;
  }
  return base + path;
}

std::string url_encode(const std::string& value) {
  static const char* hex = "0123456789ABCDEF";
  std::string encoded;
  encoded.reserve(value.size());
  for (const char character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if (std::isalnum(byte) != 0 || character == '-' || character == '_' || character == '.' ||
        character == '~' || character == '/') {
      encoded += character;
    } else {
      encoded += '%';
      encoded += hex[(byte >> 4) & 0x0F];
      encoded += hex[byte & 0x0F];
    }
  }
  return encoded;
}

std::expected<Response, std::string> Client::request(const std::string& method,
  const Url& url,
  const std::string& body,
  const std::string& content_type,
  const RequestOptions& options) {
  auto connection = Connection::open(url, options);
  if (!connection.has_value()) {
    return std::unexpected(connection.error());
  }

  std::string request_text = method + " " + url.path + " HTTP/1.1\r\n";
  request_text += "Host: " + url.host;
  if (!((url.secure && url.port == 443) || (!url.secure && url.port == 80))) {
    request_text += ":" + std::to_string(url.port);
  }
  request_text += "\r\n";
  request_text += "User-Agent: " + options.user_agent + "\r\n";
  request_text += "Accept: */*\r\n";
  request_text += "Connection: close\r\n";
  if (!options.bearer_token.empty()) {
    request_text += "Authorization: Bearer " + options.bearer_token + "\r\n";
  } else if (!options.basic_auth.empty()) {
    request_text += "Authorization: Basic " + base64_encode(options.basic_auth) + "\r\n";
  }
  if (!content_type.empty()) {
    request_text += "Content-Type: " + content_type + "\r\n";
  }
  request_text += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  request_text += "\r\n";
  request_text += body;

  std::size_t offset = 0;
  while (offset < request_text.size()) {
    auto written = connection->write(request_text.data() + offset, request_text.size() - offset);
    if (!written.has_value()) {
      return std::unexpected(written.error());
    }
    if (*written == 0) {
      return std::unexpected("the connection was closed while sending the request");
    }
    offset += *written;
  }

  std::string raw;
  char buffer[16384];
  while (raw.size() < kMaxResponseBytes) {
    auto received = connection->read(buffer, sizeof(buffer));
    if (!received.has_value()) {
      // A clean close at the end of the body is expected.
      if (raw.empty()) {
        return std::unexpected(received.error());
      }
      break;
    }
    if (*received == 0) {
      break;
    }
    raw.append(buffer, *received);
  }

  return parse_response(raw);
}

std::expected<Response, std::string> Client::get(const std::string& url, const RequestOptions& options) {
  auto parsed = parse_url(url);
  if (!parsed.has_value()) {
    return std::unexpected(parsed.error());
  }
  return request("GET", *parsed, "", "", options);
}

std::expected<Response, std::string> Client::head(const std::string& url, const RequestOptions& options) {
  auto parsed = parse_url(url);
  if (!parsed.has_value()) {
    return std::unexpected(parsed.error());
  }
  return request("HEAD", *parsed, "", "", options);
}

std::expected<Response, std::string> Client::post(const std::string& url,
  const std::string& body,
  const std::string& content_type,
  const RequestOptions& options) {
  auto parsed = parse_url(url);
  if (!parsed.has_value()) {
    return std::unexpected(parsed.error());
  }
  return request("POST", *parsed, body, content_type, options);
}

std::expected<Response, std::string> Client::put(const std::string& url,
  const std::string& body,
  const std::string& content_type,
  const RequestOptions& options) {
  auto parsed = parse_url(url);
  if (!parsed.has_value()) {
    return std::unexpected(parsed.error());
  }
  return request("PUT", *parsed, body, content_type, options);
}

std::expected<Response, std::string> Client::put_file(const std::string& url,
  const std::filesystem::path& file,
  const RequestOptions& options) {
  std::ifstream input(file, std::ios::binary);
  if (!input.is_open()) {
    return std::unexpected("failed to open file for upload: " + file.string());
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return put(url, buffer.str(), "application/octet-stream", options);
}

std::expected<Response, std::string> Client::remove(const std::string& url, const RequestOptions& options) {
  auto parsed = parse_url(url);
  if (!parsed.has_value()) {
    return std::unexpected(parsed.error());
  }
  return request("DELETE", *parsed, "", "", options);
}

}  // namespace hyprshare::http
