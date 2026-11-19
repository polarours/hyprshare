#include "hyprshare/core/inbox.hpp"

#include <openssl/evp.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <iostream>
#include <sstream>
#include <vector>

#include "hyprshare/platform/net.hpp"
#include "hyprshare/platform/process.hpp"
#include "hyprshare/util/string.hpp"
#include "hyprshare/tls/tls.hpp"
#include "hyprshare/tls/tls_transport.hpp"

namespace hyprshare::core {

namespace {

// The message the inbox uses to report that it was asked to stop.
constexpr const char* kInboxStopped = "the inbox server was stopped";

// The message the inbox uses to report that a client went away before it
// sent anything. Such a client is not an error of the server.
constexpr const char* kClientAborted = "client closed before sending a complete HTTP header";

}  // namespace

using hyprshare::platform::ScopedFd;
using hyprshare::platform::expand_home_path;
using hyprshare::platform::make_ipv4_address;
using hyprshare::platform::send_all;
using hyprshare::platform::set_socket_timeouts;
using hyprshare::tls::TlsContext;
using hyprshare::tls::TlsTransportConfig;

namespace {

// The path a peer asks for to learn which files the inbox holds.
constexpr const char* kInboxListPath = "/_hyprshare/list";

// The listing of the files in the receive directory, as JSON.
std::string build_inbox_listing(const std::filesystem::path& directory) {
  std::string body = "{\"files\":[";
  std::error_code error;

  bool first = true;
  if (std::filesystem::exists(directory, error)) {
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      if (!first) {
        body += ",";
      }
      first = false;

      const auto size = std::filesystem::file_size(entry.path(), error);
      const auto modified = std::filesystem::last_write_time(entry.path(), error).time_since_epoch().count();
      body += "{\"name\":\"" + util::escape_json(entry.path().filename().string()) + "\"";
      body += ",\"bytes\":" + std::to_string(size);
      body += ",\"modified\":" + std::to_string(modified);
      body += "}";
    }
  }

  body += "]}";
  return body;
}

struct TlsConn {
  int fd;
  TlsContext* ctx;
};

std::expected<void, std::string> tls_send_all(const TlsConn& conn, const char* data, const std::size_t size) {
  if (conn.ctx != nullptr && conn.ctx->ssl != nullptr) {
    std::size_t sent = 0;
    while (sent < size) {
      auto result = hyprshare::tls::tls_send(*conn.ctx, data + sent, size - sent);
      if (!result.has_value()) {
        return std::unexpected(result.error());
      }
      sent += static_cast<std::size_t>(*result);
    }
    return {};
  }
  return send_all(conn.fd, data, size);
}

std::expected<int, std::string> tls_recv_some(const TlsConn& conn, void* buf, const std::size_t size) {
  if (conn.ctx != nullptr && conn.ctx->ssl != nullptr) {
    return hyprshare::tls::tls_recv(*conn.ctx, buf, size);
  }
  const auto received = platform::recv_retry(conn.fd, buf, size);
  if (received < 0) {
    return std::unexpected(std::string(std::strerror(errno)));
  }
  return received;
}

struct TlsGuard {
  TlsContext& ctx;
  bool active;
  ~TlsGuard() {
    if (active) {
      hyprshare::tls::close_context(ctx);
    }
  }
};

std::string http_response(const int status, const std::string_view reason, const std::string_view body) {
  std::ostringstream stream;
  stream << "HTTP/1.1 " << status << " " << reason << "\r\n";
  stream << "Content-Type: text/plain\r\n";
  stream << "Content-Length: " << body.size() << "\r\n";
  stream << "Connection: close\r\n\r\n";
  stream << body;
  return stream.str();
}

std::expected<void, std::string> send_http_response(
  const TlsConn& conn,
  const int status,
  const std::string_view reason,
  const std::string_view body) {
  const std::string response = http_response(status, reason, body);
  return tls_send_all(conn, response.data(), response.size());
}

std::expected<std::pair<std::string, std::string>, std::string> split_once(
  const std::string_view value,
  const char separator) {
  const auto position = value.find(separator);
  if (position == std::string_view::npos) {
    return std::unexpected("missing separator");
  }
  return std::pair<std::string, std::string>{
    std::string(value.substr(0, position)),
    std::string(value.substr(position + 1)),
  };
}

std::string url_decode(std::string_view value) {
  std::string decoded;
  decoded.reserve(value.size());
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (value[index] == '%' && index + 2 < value.size()) {
      const std::string hex{value.substr(index + 1, 2)};
      char* end = nullptr;
      const auto parsed = std::strtol(hex.c_str(), &end, 16);
      if (end != nullptr && *end == '\0') {
        decoded.push_back(static_cast<char>(parsed));
        index += 2;
        continue;
      }
    }
    if (value[index] == '+') {
      decoded.push_back(' ');
    } else {
      decoded.push_back(value[index]);
    }
  }
  return decoded;
}

std::string sanitize_file_name(const std::string_view raw_name) {
  std::string name = std::filesystem::path(std::string(raw_name)).filename().string();
  name = url_decode(name);
  name = std::filesystem::path(name).filename().string();

  std::string sanitized;
  sanitized.reserve(name.size());
  for (const unsigned char ch : name) {
    if (std::isalnum(ch) != 0 || ch == '.' || ch == '-' || ch == '_') {
      sanitized.push_back(static_cast<char>(ch));
    } else {
      sanitized.push_back('_');
    }
  }

  if (sanitized.empty() || sanitized == "." || sanitized == "..") {
    return "hyprshare-upload";
  }
  return sanitized;
}

std::filesystem::path unique_destination_path(const std::filesystem::path& directory, const std::string_view file_name) {
  auto candidate = directory / file_name;
  if (!std::filesystem::exists(candidate)) {
    return candidate;
  }

  const std::filesystem::path base(file_name);
  const std::string stem = base.stem().string();
  const std::string extension = base.extension().string();
  for (int index = 1; index < 10000; ++index) {
    candidate = directory / (stem + "-" + std::to_string(index) + extension);
    if (!std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return directory / (stem + "-copy" + extension);
}

std::filesystem::path unique_receive_temp_path(const std::filesystem::path& directory) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  for (int attempt = 0; attempt < 10000; ++attempt) {
    auto candidate = directory / (".hyprshare-receive-" + std::to_string(getpid()) + "-" + std::to_string(now) + "-" +
                                  std::to_string(attempt) + ".tmp");
    if (!std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return directory / (".hyprshare-receive-" + std::to_string(getpid()) + "-copy.tmp");
}

std::expected<std::string, std::string> read_http_header(
  const TlsConn& conn,
  std::string& leftover) {
  std::string buffer;
  buffer.reserve(4096);
  char chunk[4096];
  std::size_t header_end = std::string::npos;
  while ((header_end = buffer.find("\r\n\r\n")) == std::string::npos) {
    const auto received = tls_recv_some(conn, chunk, sizeof(chunk));
    if (!received.has_value()) {
      return std::unexpected("failed while reading HTTP header: " + received.error());
    }
    if (*received == 0) {
      return std::unexpected("client closed before sending a complete HTTP header");
    }
    buffer.append(chunk, static_cast<std::size_t>(*received));
    if (buffer.size() > 65536) {
      return std::unexpected("HTTP header is too large");
    }
  }

  leftover = buffer.substr(header_end + 4);
  return buffer.substr(0, header_end);
}

struct InboxRequest {
  std::string method;
  std::string target;
  std::string file_name;
  std::uintmax_t content_length = 0;
  std::string token;
  std::string sha256;
  // Set for the request that asks for the listing of the received files.
  bool listing = false;
  // Set for the request that asks for the content of one received file.
  bool download = false;
};

bool is_sha256_hex(const std::string_view value) {
  return value.size() == 64 && std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
           return std::isxdigit(ch) != 0;
         });
}

std::expected<std::string, std::string> normalize_sha256(const std::string_view value) {
  const std::string normalized = util::to_lower(util::trim(value));
  if (!is_sha256_hex(normalized)) {
    return std::unexpected("X-Hyprshare-Sha256 must be a 64-character hexadecimal SHA-256 value");
  }
  return normalized;
}

// The checksum is computed in the process. Running sha256sum in a helper
// program would fork while the other threads of the process are running,
// which can leave the child stuck and hold up the transfer.
std::expected<std::string, std::string> compute_file_sha256(
  const std::filesystem::path& file_path) {
  std::ifstream input(file_path, std::ios::binary);
  if (!input.is_open()) {
    return std::unexpected("failed to open inbox file for hashing: " + file_path.string());
  }

  EVP_MD_CTX* context = EVP_MD_CTX_new();
  if (context == nullptr) {
    return std::unexpected("failed to create the hashing context");
  }

  std::string hash;
  {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_length = 0;

    if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1) {
      EVP_MD_CTX_free(context);
      return std::unexpected("failed to start the hashing");
    }

    std::array<char, 65536> buffer{};
    while (input.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || input.gcount() > 0) {
      if (EVP_DigestUpdate(context, buffer.data(), static_cast<std::size_t>(input.gcount())) != 1) {
        EVP_MD_CTX_free(context);
        return std::unexpected("failed to hash inbox file `" + file_path.string() + "`");
      }
    }

    if (EVP_DigestFinal_ex(context, digest, &digest_length) != 1) {
      EVP_MD_CTX_free(context);
      return std::unexpected("failed to finish the hashing");
    }

    static const char* hex = "0123456789abcdef";
    hash.reserve(digest_length * 2);
    for (unsigned int index = 0; index < digest_length; ++index) {
      hash += hex[(digest[index] >> 4) & 0x0F];
      hash += hex[digest[index] & 0x0F];
    }
  }

  EVP_MD_CTX_free(context);
  return hash;
}

std::expected<InboxRequest, std::string> parse_inbox_request(const std::string& header) {
  std::istringstream stream(header);
  std::string request_line;
  if (!std::getline(stream, request_line)) {
    return std::unexpected("HTTP request is empty");
  }
  if (!request_line.empty() && request_line.back() == '\r') {
    request_line.pop_back();
  }

  std::istringstream request_stream(request_line);
  std::string version;
  InboxRequest request;
  if (!(request_stream >> request.method >> request.target >> version)) {
    return std::unexpected("invalid HTTP request line");
  }
  if (request.method != "PUT" && request.method != "GET") {
    return std::unexpected("only HTTP PUT uploads and GET listings are supported");
  }

  std::map<std::string, std::string> headers;
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const auto field = split_once(line, ':');
    if (!field.has_value()) {
      continue;
    }
    headers.emplace(util::to_lower(util::trim(field->first)), util::trim(field->second));
  }

  const auto length_it = headers.find("content-length");
  if (length_it != headers.end()) {
    try {
      request.content_length = static_cast<std::uintmax_t>(std::stoull(length_it->second));
    } catch (const std::exception&) {
      return std::unexpected("Content-Length is not a valid number");
    }
  } else if (request.method == "PUT") {
    return std::unexpected("HTTP PUT requires Content-Length");
  }

  if (const auto token_it = headers.find("x-hyprshare-token"); token_it != headers.end()) {
    request.token = token_it->second;
  }
  if (const auto sha256_it = headers.find("x-hyprshare-sha256"); sha256_it != headers.end()) {
    const auto sha256 = normalize_sha256(sha256_it->second);
    if (!sha256.has_value()) {
      return std::unexpected(sha256.error());
    }
    request.sha256 = *sha256;
  }

  const auto query_start = request.target.find('?');
  const std::string path = request.target.substr(0, query_start);
  request.file_name = sanitize_file_name(path);
  if (request.method == "GET") {
    if (path == kInboxListPath || path == "/") {
      request.listing = true;
    } else {
      request.download = true;
    }
  }
  if (query_start != std::string::npos) {
    const std::vector<std::string> query_fields = util::split(request.target.substr(query_start + 1), '&');
    for (const auto& query_field : query_fields) {
      const auto key_value = split_once(query_field, '=');
      if (!key_value.has_value()) {
        continue;
      }
      if (key_value->first == "name") {
        request.file_name = sanitize_file_name(key_value->second);
      } else if (key_value->first == "token" && request.token.empty()) {
        request.token = url_decode(key_value->second);
      }
    }
  }

  return request;
}

std::expected<std::uintmax_t, std::string> receive_body_to_file(
  const TlsConn& conn,
  std::string leftover,
  const std::uintmax_t content_length,
  const std::filesystem::path& destination_path) {
  std::ofstream output(destination_path, std::ios::binary);
  if (!output.is_open()) {
    return std::unexpected("failed to open inbox destination for writing: " + destination_path.string());
  }

  std::uintmax_t written = 0;
  if (!leftover.empty()) {
    const auto size = std::min<std::uintmax_t>(leftover.size(), content_length);
    output.write(leftover.data(), static_cast<std::streamsize>(size));
    written += size;
  }

  char chunk[8192];
  while (written < content_length) {
    const auto remaining = content_length - written;
    const auto read_size = std::min<std::uintmax_t>(sizeof(chunk), remaining);
    const auto received = tls_recv_some(conn, chunk, static_cast<std::size_t>(read_size));
    if (!received.has_value()) {
      return std::unexpected("failed while receiving inbox upload: " + received.error());
    }
    if (*received == 0) {
      return std::unexpected("client closed before upload completed");
    }
    output.write(chunk, *received);
    written += static_cast<std::uintmax_t>(*received);
  }

  return written;
}

std::expected<ScopedFd, std::string> connect_socket(const std::string_view host, const std::uint16_t port) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo* raw_results = nullptr;
  const std::string host_text(host);
  const std::string service = std::to_string(port);
  const int resolved = getaddrinfo(host_text.c_str(), service.c_str(), &hints, &raw_results);
  if (resolved != 0) {
    return std::unexpected("failed to resolve inbox receiver host `" + host_text + "`: " + gai_strerror(resolved));
  }

  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> results(raw_results, freeaddrinfo);
  std::string last_error = "no usable IPv4 address was returned";
  for (const addrinfo* current = results.get(); current != nullptr; current = current->ai_next) {
    ScopedFd socket_fd(socket(current->ai_family, current->ai_socktype, current->ai_protocol));
    if (!socket_fd.valid()) {
      last_error = std::strerror(errno);
      continue;
    }

    auto timeout_result = set_socket_timeouts(socket_fd.get());
    if (!timeout_result.has_value()) {
      return std::unexpected(timeout_result.error());
    }

    if (connect(socket_fd.get(), current->ai_addr, current->ai_addrlen) == 0) {
      return socket_fd;
    }
    last_error = std::strerror(errno);
  }

  return std::unexpected("failed to connect to inbox receiver `" + host_text + ":" + service + "`: " + last_error);
}

std::expected<std::filesystem::path, std::string> make_upload_temp_dir() {
  std::error_code error;
  const auto base = std::filesystem::temp_directory_path(error);
  if (error) {
    return std::unexpected("failed to resolve temporary directory: " + error.message());
  }

  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  for (int attempt = 0; attempt < 100; ++attempt) {
    const auto candidate = base / ("hyprshare-inbox-upload-" + std::to_string(getpid()) + "-" + std::to_string(now) + "-" +
                                   std::to_string(attempt));
    std::filesystem::create_directory(candidate, error);
    if (!error) {
      return candidate;
    }
    if (error != std::errc::file_exists) {
      return std::unexpected("failed to create temporary upload directory `" + candidate.string() + "`: " + error.message());
    }
  }

  return std::unexpected("failed to create a unique temporary upload directory");
}

std::expected<std::filesystem::path, std::string> archive_directory_for_upload(
  const std::filesystem::path& absolute_directory,
  const std::filesystem::path& temp_dir,
  const std::string_view archive_name,
  const platform::ProcessRunner& runner) {
  if (!runner.command_exists("tar")) {
    return std::unexpected("directory upload requires `tar` to create a portable archive");
  }
  if (absolute_directory.filename().empty()) {
    return std::unexpected("refusing to upload a directory without a stable name: " + absolute_directory.string());
  }

  const auto archive_path = temp_dir / archive_name;
  const std::string command = "tar -czf " + util::shell_quote(archive_path.string()) + " -C " +
                              util::shell_quote(absolute_directory.parent_path().string()) + " " +
                              util::shell_quote(absolute_directory.filename().string());
  const auto result = runner.run_shell(command);
  if (result.exit_code != 0) {
    return std::unexpected("failed to archive directory with tar: " + result.output);
  }
  if (!std::filesystem::is_regular_file(archive_path)) {
    return std::unexpected("tar did not create expected archive: " + archive_path.string());
  }
  return archive_path;
}

std::expected<std::filesystem::path, std::string> normalize_existing_archive_path(const std::filesystem::path& archive_path) {
  const auto expanded = expand_home_path(archive_path);
  if (expanded.empty()) {
    return std::unexpected("failed to expand archive path `" + archive_path.string() + "`");
  }

  std::error_code error;
  const auto absolute = std::filesystem::absolute(expanded, error);
  if (error) {
    return std::unexpected("failed to resolve archive path `" + archive_path.string() + "`: " + error.message());
  }
  if (!std::filesystem::exists(absolute)) {
    return std::unexpected("archive path does not exist: " + absolute.string());
  }
  if (!std::filesystem::is_regular_file(absolute)) {
    return std::unexpected("archive path is not a regular file: " + absolute.string());
  }
  return absolute;
}

bool contains_control_character(const std::string_view value) {
  return std::any_of(value.begin(), value.end(), [](const unsigned char ch) {
    return ch < 32 || ch == 127;
  });
}

std::expected<void, std::string> validate_archive_entry_type_listing(const std::string_view verbose_listing) {
  std::istringstream stream{std::string(verbose_listing)};
  std::string line;
  while (std::getline(stream, line)) {
    line = util::trim(line);
    if (line.empty()) {
      continue;
    }

    const char type = line.front();
    if (type != '-' && type != 'd') {
      return std::unexpected("archive contains unsupported non-file entry: " + line);
    }
  }
  return {};
}

std::expected<InboxArchiveInspection, std::string> validate_archive_listing(
  const std::filesystem::path& archive_path,
  const std::string_view listing) {
  std::istringstream stream{std::string(listing)};
  std::string line;
  std::string top_level;
  InboxArchiveInspection inspection;
  inspection.archive_path = archive_path;

  while (std::getline(stream, line)) {
    const std::string entry = util::trim(line);
    if (entry.empty()) {
      continue;
    }
    if (contains_control_character(entry)) {
      return std::unexpected("archive contains an entry with control characters");
    }

    const std::filesystem::path entry_path(entry);
    if (entry_path.is_absolute()) {
      return std::unexpected("archive contains absolute path: " + entry);
    }

    const auto normalized = entry_path.lexically_normal();
    std::string current_top_level;
    for (const auto& component : normalized) {
      const auto value = component.string();
      if (value.empty() || value == ".") {
        continue;
      }
      if (value == "..") {
        return std::unexpected("archive contains parent traversal path: " + entry);
      }
      current_top_level = value;
      break;
    }

    if (current_top_level.empty()) {
      return std::unexpected("archive contains an invalid entry: " + entry);
    }
    if (top_level.empty()) {
      top_level = current_top_level;
    } else if (top_level != current_top_level) {
      return std::unexpected("archive must contain a single top-level entry to extract safely");
    }

    inspection.entries.push_back(entry);
  }

  if (inspection.entries.empty()) {
    return std::unexpected("archive is empty");
  }
  inspection.top_level = top_level;
  return inspection;
}

std::expected<std::filesystem::path, std::string> prepare_extract_destination(
  const std::filesystem::path& destination_dir,
  const bool dry_run) {
  const auto expanded = expand_home_path(destination_dir);
  if (expanded.empty()) {
    return std::unexpected("failed to expand extraction destination `" + destination_dir.string() + "`");
  }

  std::error_code error;
  const auto absolute = std::filesystem::absolute(expanded, error);
  if (error) {
    return std::unexpected("failed to resolve extraction destination `" + destination_dir.string() + "`: " + error.message());
  }
  if (!dry_run) {
    std::filesystem::create_directories(absolute, error);
    if (error) {
      return std::unexpected("failed to create extraction destination `" + absolute.string() + "`: " + error.message());
    }
  }
  return absolute;
}

std::filesystem::path unique_child_path(const std::filesystem::path& directory, const std::string_view child_name) {
  auto candidate = directory / child_name;
  if (!std::filesystem::exists(candidate)) {
    return candidate;
  }

  const std::filesystem::path base(child_name);
  const std::string stem = base.stem().string();
  const std::string extension = base.extension().string();
  for (int index = 1; index < 10000; ++index) {
    candidate = directory / (stem + "-" + std::to_string(index) + extension);
    if (!std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return directory / (stem + "-copy" + extension);
}

std::expected<std::filesystem::path, std::string> make_extract_temp_dir(const std::filesystem::path& destination_dir) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  std::error_code error;
  for (int attempt = 0; attempt < 100; ++attempt) {
    const auto candidate = destination_dir / (".hyprshare-extract-" + std::to_string(getpid()) + "-" + std::to_string(now) + "-" +
                                             std::to_string(attempt));
    std::filesystem::create_directory(candidate, error);
    if (!error) {
      return candidate;
    }
    if (error != std::errc::file_exists) {
      return std::unexpected("failed to create temporary extraction directory `" + candidate.string() + "`: " + error.message());
    }
  }
  return std::unexpected("failed to create a unique temporary extraction directory");
}

std::expected<void, std::string> validate_extracted_tree(const std::filesystem::path& extracted_path) {
  std::error_code error;
  const auto root_status = std::filesystem::symlink_status(extracted_path, error);
  if (error) {
    return std::unexpected("failed to inspect extracted path `" + extracted_path.string() + "`: " + error.message());
  }
  if (!std::filesystem::is_directory(root_status) && !std::filesystem::is_regular_file(root_status)) {
    return std::unexpected("archive extracted an unsupported top-level file type: " + extracted_path.string());
  }
  if (!std::filesystem::is_directory(root_status)) {
    return {};
  }

  for (std::filesystem::recursive_directory_iterator iterator(extracted_path, error), end; iterator != end; iterator.increment(error)) {
    if (error) {
      return std::unexpected("failed to enumerate extracted archive `" + extracted_path.string() + "`: " + error.message());
    }
    const auto status = iterator->symlink_status(error);
    if (error) {
      return std::unexpected("failed to inspect extracted archive entry `" + iterator->path().string() + "`: " + error.message());
    }
    if (!std::filesystem::is_directory(status) && !std::filesystem::is_regular_file(status)) {
      return std::unexpected("archive extracted an unsupported file type: " + iterator->path().string());
    }
  }
  return {};
}

std::expected<std::filesystem::path, std::string> prepare_destination_dir(const std::filesystem::path& destination_dir) {
  std::error_code error;
  const auto expanded_destination = expand_home_path(destination_dir);
  if (expanded_destination.empty()) {
    return std::unexpected("failed to expand inbox directory `" + destination_dir.string() + "`");
  }

  const auto absolute_destination = std::filesystem::absolute(expanded_destination, error);
  if (error) {
    return std::unexpected("failed to resolve inbox directory `" + destination_dir.string() + "`: " + error.message());
  }
  std::filesystem::create_directories(absolute_destination, error);
  if (error) {
    return std::unexpected("failed to create inbox directory `" + absolute_destination.string() + "`: " + error.message());
  }
  return absolute_destination;
}

std::expected<ScopedFd, std::string> create_inbox_listener(
  const std::string_view bind_host,
  const std::uint16_t port) {
  const auto address = make_ipv4_address(bind_host, port);
  if (!address.has_value()) {
    return std::unexpected(address.error());
  }

  ScopedFd listen_fd(socket(AF_INET, SOCK_STREAM, 0));
  if (!listen_fd.valid()) {
    return std::unexpected("failed to create inbox server socket: " + std::string(std::strerror(errno)));
  }

  const int reuse_addr = 1;
  if (setsockopt(listen_fd.get(), SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) != 0) {
    return std::unexpected("failed to configure inbox server socket: " + std::string(std::strerror(errno)));
  }
  if (bind(listen_fd.get(), reinterpret_cast<const sockaddr*>(&*address), sizeof(*address)) != 0) {
    return std::unexpected("failed to bind inbox server socket: " + std::string(std::strerror(errno)));
  }
  if (listen(listen_fd.get(), 1) != 0) {
    return std::unexpected("failed to listen for inbox upload: " + std::string(std::strerror(errno)));
  }
  return listen_fd;
}

std::expected<void, std::string> wait_for_inbox_client(
  const int listen_fd,
  const std::uint32_t idle_timeout_seconds,
  const std::atomic<bool>* stop) {
  if (idle_timeout_seconds == 0 && stop == nullptr) {
    return {};
  }

  // When a stop flag is given, the wait is cut into short pieces, so that the
  // server notices the request to stop without waiting for the whole timeout.
  const bool has_deadline = idle_timeout_seconds > 0;
  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::seconds(idle_timeout_seconds);

  while (true) {
    if (stop != nullptr && stop->load()) {
      return std::unexpected(kInboxStopped);
    }

    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(listen_fd, &read_set);

    timeval timeout{};
    if (stop != nullptr) {
      timeout.tv_sec = 0;
      timeout.tv_usec = 200000;
    } else {
      timeout.tv_sec = static_cast<decltype(timeout.tv_sec)>(idle_timeout_seconds);
    }

    const int ready = select(listen_fd + 1, &read_set, nullptr, nullptr, &timeout);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      return std::unexpected("failed while waiting for inbox upload: " + std::string(std::strerror(errno)));
    }
    if (ready > 0) {
      return {};
    }

    if (stop == nullptr) {
      return std::unexpected("timed out waiting for inbox upload");
    }
    if (has_deadline && std::chrono::steady_clock::now() >= deadline) {
      return std::unexpected("timed out waiting for inbox upload");
    }
  }
}

// What one connection to the inbox produced.
struct InboxOutcome {
  model::InboxReceivedFile file;
  // A listing is not an upload and does not count towards the upload count.
  bool listing = false;
  // The request was refused. The server keeps running, because one client
  // with a wrong token must not take the inbox down.
  bool rejected = false;
  // A download is not an upload either.
  bool downloaded = false;
};

std::expected<InboxOutcome, std::string> receive_one_inbox_upload(
  const int listen_fd,
  const std::filesystem::path& absolute_destination,
  const std::string_view access_token,
  const std::uint32_t idle_timeout_seconds,
  const std::uintmax_t max_upload_bytes,
  const platform::ProcessRunner* checksum_runner,
  const TlsTransportConfig& tls_config,
  const std::atomic<bool>* stop) {
  std::error_code error;
  const auto wait_result = wait_for_inbox_client(listen_fd, idle_timeout_seconds, stop);
  if (!wait_result.has_value()) {
    return std::unexpected(wait_result.error());
  }

  sockaddr_in client_address{};
  socklen_t client_address_length = sizeof(client_address);
  ScopedFd client_fd(accept(listen_fd, reinterpret_cast<sockaddr*>(&client_address), &client_address_length));
  if (!client_fd.valid()) {
    return std::unexpected("failed to accept inbox upload client: " + std::string(std::strerror(errno)));
  }
  auto timeout_result = set_socket_timeouts(client_fd.get());
  if (!timeout_result.has_value()) {
    return std::unexpected(timeout_result.error());
  }

  TlsContext tls_ctx{};
  if (tls_config.enabled) {
    auto upgrade = hyprshare::tls::upgrade_server_socket(client_fd.get(), tls_config, tls_ctx);
    if (!upgrade.has_value()) {
      // The client could not be served. That concerns this client alone, so
      // the inbox keeps running for the next one.
      hyprshare::tls::close_context(tls_ctx);
      InboxOutcome outcome;
      outcome.rejected = true;
      return outcome;
    }
  }
  TlsGuard tls_guard{tls_ctx, tls_config.enabled};
  const TlsConn conn{client_fd.get(), tls_config.enabled ? &tls_ctx : nullptr};

  std::string leftover;
  const auto header = read_http_header(conn, leftover);
  if (!header.has_value()) {
    // A client that sends nothing, or nonsense, is rejected. The inbox keeps
    // running for the next one.
    InboxOutcome outcome;
    outcome.rejected = true;
    return outcome;
  }

  const auto request = parse_inbox_request(*header);
  if (!request.has_value()) {
    InboxOutcome outcome;
    outcome.rejected = true;
    return outcome;
  }

  if (!access_token.empty() && request->token != access_token) {
    static_cast<void>(send_http_response(conn, 403, "Forbidden", "access token required or invalid\n"));
    InboxOutcome outcome;
    outcome.rejected = true;
    return outcome;
  }

  if (request->listing) {
    const std::string listing = build_inbox_listing(absolute_destination);
    static_cast<void>(send_http_response(conn, 200, "OK", listing));
    InboxOutcome outcome;
    outcome.listing = true;
    outcome.file.file_name = kInboxListPath;
    outcome.file.destination_path = absolute_destination.string();
    return outcome;
  }

  if (request->download) {
    const auto source = absolute_destination / request->file_name;
    std::error_code file_error;
    if (!std::filesystem::is_regular_file(source, file_error)) {
      static_cast<void>(send_http_response(conn, 404, "Not Found", "no such file\n"));
      InboxOutcome outcome;
      outcome.rejected = true;
      return outcome;
    }

    std::ifstream input(source, std::ios::binary);
    if (!input.is_open()) {
      static_cast<void>(send_http_response(conn, 500, "Internal Server Error", "failed to read the file\n"));
      InboxOutcome outcome;
      outcome.rejected = true;
      return outcome;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string content = buffer.str();
    static_cast<void>(send_http_response(conn, 200, "OK", content));

    InboxOutcome outcome;
    outcome.downloaded = true;
    outcome.file.file_name = request->file_name;
    outcome.file.destination_path = source.string();
    outcome.file.bytes_received = content.size();
    return outcome;
  }

  if (max_upload_bytes > 0 && request->content_length > max_upload_bytes) {
    static_cast<void>(send_http_response(conn, 413, "Payload Too Large", "upload exceeds max-bytes limit\n"));
    return std::unexpected("upload exceeds max-bytes limit");
  }

  if (!request->sha256.empty() && checksum_runner == nullptr) {
    static_cast<void>(
      send_http_response(conn, 500, "Internal Server Error", "checksum verification unavailable\n"));
    return std::unexpected("checksum verification unavailable");
  }

  auto destination_path = unique_destination_path(absolute_destination, request->file_name);
  const auto temp_path = unique_receive_temp_path(absolute_destination);
  const auto received = receive_body_to_file(conn, std::move(leftover), request->content_length, temp_path);
  if (!received.has_value()) {
    std::filesystem::remove(temp_path, error);
    static_cast<void>(send_http_response(conn, 500, "Internal Server Error", received.error() + "\n"));
    return std::unexpected(received.error());
  }

  if (!request->sha256.empty()) {
    const auto actual_sha256 = compute_file_sha256(temp_path);
    if (!actual_sha256.has_value()) {
      std::filesystem::remove(temp_path, error);
      static_cast<void>(send_http_response(conn, 500, "Internal Server Error", actual_sha256.error() + "\n"));
      return std::unexpected(actual_sha256.error());
    }
    if (*actual_sha256 != request->sha256) {
      std::filesystem::remove(temp_path, error);
      static_cast<void>(send_http_response(conn, 400, "Bad Request", "sha256 checksum mismatch\n"));
      return std::unexpected("sha256 checksum mismatch");
    }
  }

  if (std::filesystem::exists(destination_path)) {
    destination_path = unique_destination_path(absolute_destination, request->file_name);
  }
  std::filesystem::rename(temp_path, destination_path, error);
  if (error) {
    std::filesystem::remove(temp_path, error);
    static_cast<void>(send_http_response(conn, 500, "Internal Server Error", "failed to finalize inbox upload\n"));
    return std::unexpected("failed to finalize inbox upload: " + error.message());
  }

  static_cast<void>(send_http_response(conn, 201, "Created", "uploaded\n"));

  InboxOutcome outcome;
  outcome.file.file_name = destination_path.filename().string();
  outcome.file.destination_path = destination_path.string();
  outcome.file.bytes_received = *received;
  outcome.file.sha256 = request->sha256;
  return outcome;
}

bool is_idle_timeout_error(const std::string_view error) {
  return error.find("timed out waiting for inbox upload") != std::string_view::npos;
}

// Reads the file entries out of the listing that the inbox answers with.
std::vector<InboxRemoteFile> parse_inbox_listing(const std::string& body) {
  std::vector<InboxRemoteFile> files;

  std::size_t position = 0;
  while ((position = body.find("\"name\"", position)) != std::string::npos) {
    InboxRemoteFile file;

    const auto read_string = [&body](const std::size_t from, std::string& out) -> std::size_t {
      const auto start = body.find('"', from + 7);
      if (start == std::string::npos) {
        return std::string::npos;
      }
      const auto end = body.find('"', start + 1);
      if (end == std::string::npos) {
        return std::string::npos;
      }
      out = body.substr(start + 1, end - start - 1);
      return end;
    };

    const auto name_end = read_string(position, file.name);
    if (name_end == std::string::npos) {
      break;
    }

    // The number follows the key and a colon.
    const auto read_number = [&body](const std::size_t from, std::int64_t& out) -> bool {
      const auto colon = body.find(':', from);
      if (colon == std::string::npos) {
        return false;
      }
      std::size_t end = colon + 1;
      while (end < body.size() &&
             (std::isdigit(static_cast<unsigned char>(body[end])) != 0 || body[end] == '-')) {
        ++end;
      }
      const std::string digits = body.substr(colon + 1, end - colon - 1);
      if (digits.empty()) {
        return false;
      }
      try {
        out = std::stoll(digits);
      } catch (const std::exception&) {
        return false;
      }
      return true;
    };

    const auto bytes_position = body.find("\"bytes\"", name_end);
    if (bytes_position != std::string::npos) {
      std::int64_t bytes = 0;
      if (read_number(bytes_position, bytes)) {
        file.bytes = static_cast<std::uintmax_t>(bytes);
      }
    }

    const auto modified_position = body.find("\"modified\"", name_end);
    if (modified_position != std::string::npos) {
      std::int64_t modified = 0;
      if (read_number(modified_position, modified)) {
        file.modified_at_unix = modified;
      }
    }

    files.push_back(file);
    position = name_end;
  }

  return files;
}

}  // namespace

std::expected<model::InboxReceivedFile, std::string> serve_inbox_once(
  const std::string_view bind_host,
  const std::uint16_t port,
  const std::filesystem::path& destination_dir,
  const std::string_view access_token,
  const std::uint32_t idle_timeout_seconds,
  const std::uintmax_t max_upload_bytes,
  const platform::ProcessRunner* checksum_runner,
  const TlsTransportConfig& tls_config) {
  const auto received =
    serve_inbox(bind_host, port, destination_dir, 1, access_token, idle_timeout_seconds, max_upload_bytes, checksum_runner, tls_config);
  if (!received.has_value()) {
    return std::unexpected(received.error());
  }
  if (received->empty()) {
    return std::unexpected("inbox receiver did not receive a file");
  }
  return received->front();
}

std::expected<std::vector<model::InboxReceivedFile>, std::string> serve_inbox(
  const std::string_view bind_host,
  const std::uint16_t port,
  const std::filesystem::path& destination_dir,
  const std::uint32_t upload_count,
  const std::string_view access_token,
  const std::uint32_t idle_timeout_seconds,
  const std::uintmax_t max_upload_bytes,
  const platform::ProcessRunner* checksum_runner,
  const TlsTransportConfig& tls_config,
  const std::atomic<bool>* stop) {
  if (upload_count == 0 && idle_timeout_seconds == 0 && stop == nullptr) {
    return std::unexpected("inbox count 0 means receive until idle and requires --timeout");
  }

  const auto absolute_destination = prepare_destination_dir(destination_dir);
  if (!absolute_destination.has_value()) {
    return std::unexpected(absolute_destination.error());
  }

  auto listen_fd = create_inbox_listener(bind_host, port);
  if (!listen_fd.has_value()) {
    return std::unexpected(listen_fd.error());
  }

  std::vector<model::InboxReceivedFile> received_files;
  if (upload_count > 0) {
    received_files.reserve(upload_count);
  }

  const auto target_count = upload_count == 0 ? std::numeric_limits<std::uint32_t>::max() : upload_count;
  std::uint32_t uploads = 0;
  while (uploads < target_count) {
    auto received = receive_one_inbox_upload(
      listen_fd->get(), *absolute_destination, access_token, idle_timeout_seconds, max_upload_bytes, checksum_runner, tls_config, stop);
    if (!received.has_value()) {
      if (received.error() == kInboxStopped) {
        break;
      }
      if (received.error() == kClientAborted) {
        // The client went away without sending anything. The listener keeps
        // the port and waits for the next one.
        continue;
      }
      if (upload_count == 0 && is_idle_timeout_error(received.error())) {
        break;
      }
      return std::unexpected(received.error());
    }
    if (received->listing || received->rejected || received->downloaded) {
      // Neither a listing, nor a download, nor a refused request counts
      // towards the uploads that were asked for, so the loop does not
      // advance here.
      continue;
    }
    received_files.push_back(received->file);
    ++uploads;
  }
  return received_files;
}

std::expected<std::uintmax_t, std::string> put_inbox_file(
  const std::string_view host,
  const std::uint16_t port,
  const std::filesystem::path& file_path,
  const std::string_view remote_name,
  const std::string_view access_token,
  const platform::ProcessRunner* checksum_runner,
  const TlsTransportConfig& tls_config,
  const UploadProgressCallback* progress) {
  std::error_code error;
  const auto absolute_file = std::filesystem::absolute(file_path, error);
  if (error) {
    return std::unexpected("failed to resolve upload file `" + file_path.string() + "`: " + error.message());
  }
  if (!std::filesystem::is_regular_file(absolute_file)) {
    return std::unexpected("upload path is not a regular file: " + absolute_file.string());
  }

  const auto file_size = std::filesystem::file_size(absolute_file, error);
  if (error) {
    return std::unexpected("failed to read upload file size `" + absolute_file.string() + "`: " + error.message());
  }

  std::string sha256;
  if (checksum_runner != nullptr) {
    const auto computed_sha256 = compute_file_sha256(absolute_file);
    if (!computed_sha256.has_value()) {
      return std::unexpected(computed_sha256.error());
    }
    sha256 = *computed_sha256;
  }

  auto socket_fd = connect_socket(host, port);
  if (!socket_fd.has_value()) {
    return std::unexpected(socket_fd.error());
  }

  TlsContext tls_ctx{};
  if (tls_config.enabled) {
    auto upgrade = hyprshare::tls::upgrade_client_socket(socket_fd->get(), tls_config, tls_ctx);
    if (!upgrade.has_value()) {
      return std::unexpected(upgrade.error());
    }
  }
  TlsGuard tls_guard{tls_ctx, tls_config.enabled};
  const TlsConn conn{socket_fd->get(), tls_config.enabled ? &tls_ctx : nullptr};

  const std::string name = sanitize_file_name(remote_name.empty() ? absolute_file.filename().string() : std::string(remote_name));
  std::ostringstream header;
  header << "PUT /" << name << " HTTP/1.1\r\n";
  header << "Host: " << host << ":" << port << "\r\n";
  header << "Content-Length: " << file_size << "\r\n";
  if (!access_token.empty()) {
    header << "X-Hyprshare-Token: " << access_token << "\r\n";
  }
  if (!sha256.empty()) {
    header << "X-Hyprshare-Sha256: " << sha256 << "\r\n";
  }
  header << "Connection: close\r\n\r\n";

  const std::string header_text = header.str();
  auto send_result = tls_send_all(conn, header_text.data(), header_text.size());
  if (!send_result.has_value()) {
    return std::unexpected(send_result.error());
  }

  std::ifstream input(absolute_file, std::ios::binary);
  if (!input.is_open()) {
    return std::unexpected("failed to open upload file: " + absolute_file.string());
  }

  std::uintmax_t sent_bytes = 0;
  char buffer[8192];
  while (input.good()) {
    input.read(buffer, sizeof(buffer));
    const auto count = input.gcount();
    if (count <= 0) {
      break;
    }
    send_result = tls_send_all(conn, buffer, static_cast<std::size_t>(count));
    if (!send_result.has_value()) {
      return std::unexpected(send_result.error());
    }
    sent_bytes += static_cast<std::uintmax_t>(count);
    if (progress != nullptr) {
      (*progress)(sent_bytes, file_size);
    }
  }

  char response[256];
  const auto recv_result = tls_recv_some(conn, response, sizeof(response));
  if (!recv_result.has_value()) {
    return std::unexpected("failed to read inbox response: " + recv_result.error());
  }
  if (*recv_result == 0) {
    return std::unexpected("inbox receiver closed without an HTTP response");
  }
  const std::string response_text(response, static_cast<std::size_t>(*recv_result));
  if (response_text.find("HTTP/1.1 201") != 0) {
    return std::unexpected("inbox receiver rejected upload: " + util::trim(response_text));
  }

  return sent_bytes;
}

std::expected<InboxUploadResult, std::string> put_inbox_path(
  const std::string_view host,
  const std::uint16_t port,
  const std::filesystem::path& path,
  const platform::ProcessRunner& runner,
  const std::string_view remote_name,
  const std::string_view access_token,
  const TlsTransportConfig& tls_config,
  const UploadProgressCallback* progress) {
  std::error_code error;
  const auto absolute_path = std::filesystem::absolute(path, error);
  if (error) {
    return std::unexpected("failed to resolve upload path `" + path.string() + "`: " + error.message());
  }

  if (std::filesystem::is_regular_file(absolute_path)) {
    const std::string upload_name =
      sanitize_file_name(remote_name.empty() ? absolute_path.filename().string() : std::string(remote_name));
    std::string sha256;
    if (runner.command_exists("sha256sum")) {
      const auto computed_sha256 = compute_file_sha256(absolute_path);
      if (!computed_sha256.has_value()) {
        return std::unexpected(computed_sha256.error());
      }
      sha256 = *computed_sha256;
    }
    const auto sent = put_inbox_file(host, port, absolute_path, upload_name, access_token, &runner, tls_config, progress);
    if (!sent.has_value()) {
      return std::unexpected(sent.error());
    }
    return InboxUploadResult{
      .bytes_sent = *sent,
      .uploaded_path = absolute_path,
      .remote_name = upload_name,
      .archived_directory = false,
      .sha256 = sha256,
    };
  }

  if (!std::filesystem::is_directory(absolute_path)) {
    return std::unexpected("upload path is neither a regular file nor a directory: " + absolute_path.string());
  }

  const auto temp_dir = make_upload_temp_dir();
  if (!temp_dir.has_value()) {
    return std::unexpected(temp_dir.error());
  }

  const std::string base_name = sanitize_file_name(absolute_path.filename().string());
  const std::string upload_name = sanitize_file_name(remote_name.empty() ? base_name + ".tar.gz" : std::string(remote_name));
  const auto archive_path = archive_directory_for_upload(absolute_path, *temp_dir, upload_name, runner);
  if (!archive_path.has_value()) {
    std::filesystem::remove_all(*temp_dir, error);
    return std::unexpected(archive_path.error());
  }

  std::string sha256;
  if (runner.command_exists("sha256sum")) {
    const auto computed_sha256 = compute_file_sha256(*archive_path);
    if (!computed_sha256.has_value()) {
      std::filesystem::remove_all(*temp_dir, error);
      return std::unexpected(computed_sha256.error());
    }
    sha256 = *computed_sha256;
  }

  const auto sent = put_inbox_file(host, port, *archive_path, upload_name, access_token, &runner, tls_config);
  std::filesystem::remove_all(*temp_dir, error);
  if (!sent.has_value()) {
    return std::unexpected(sent.error());
  }

  return InboxUploadResult{
    .bytes_sent = *sent,
    .uploaded_path = absolute_path,
    .remote_name = upload_name,
    .archived_directory = true,
    .sha256 = sha256,
  };
}

std::expected<std::vector<InboxRemoteFile>, std::string> list_inbox_files(
  const std::string_view host,
  const std::uint16_t port,
  const std::string_view access_token,
  const tls::TlsTransportConfig& tls_config) {
  const auto address = make_ipv4_address(host, port);
  if (!address.has_value()) {
    return std::unexpected(address.error());
  }

  auto socket_fd = platform::connect_transport_socket(host, port);
  if (!socket_fd.has_value()) {
    return std::unexpected(socket_fd.error());
  }

  TlsContext tls_ctx{};
  if (tls_config.enabled) {
    auto upgrade = hyprshare::tls::upgrade_client_socket(socket_fd->get(), tls_config, tls_ctx);
    if (!upgrade.has_value()) {
      return std::unexpected(upgrade.error());
    }
  }
  TlsGuard tls_guard{tls_ctx, tls_config.enabled};
  const TlsConn conn{socket_fd->get(), tls_config.enabled ? &tls_ctx : nullptr};

  std::ostringstream header;
  header << "GET " << kInboxListPath << " HTTP/1.1\r\n";
  header << "Host: " << host << ":" << port << "\r\n";
  if (!access_token.empty()) {
    header << "X-Hyprshare-Token: " << access_token << "\r\n";
  }
  header << "Connection: close\r\n\r\n";

  const std::string header_text = header.str();
  auto send_result = tls_send_all(conn, header_text.data(), header_text.size());
  if (!send_result.has_value()) {
    return std::unexpected(send_result.error());
  }

  std::string response;
  char chunk[4096];
  while (true) {
    const auto received = tls_recv_some(conn, chunk, sizeof(chunk));
    if (!received.has_value()) {
      if (response.empty()) {
        return std::unexpected("failed to read the inbox listing: " + received.error());
      }
      break;
    }
    if (*received == 0) {
      break;
    }
    response.append(chunk, static_cast<std::size_t>(*received));
  }

  const auto header_end = response.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    return std::unexpected("the inbox did not answer with a complete HTTP response");
  }
  const std::string status_line = response.substr(0, response.find("\r\n"));
  if (status_line.find("200") == std::string::npos) {
    return std::unexpected("the inbox rejected the listing request: " + util::trim(status_line));
  }

  return parse_inbox_listing(response.substr(header_end + 4));
}

std::expected<std::uintmax_t, std::string> get_inbox_file(
  const std::string_view host,
  const std::uint16_t port,
  const std::string& remote_name,
  const std::filesystem::path& destination,
  const std::string_view access_token,
  const tls::TlsTransportConfig& tls_config) {
  auto socket_fd = platform::connect_transport_socket(host, port);
  if (!socket_fd.has_value()) {
    return std::unexpected(socket_fd.error());
  }

  TlsContext tls_ctx{};
  if (tls_config.enabled) {
    auto upgrade = hyprshare::tls::upgrade_client_socket(socket_fd->get(), tls_config, tls_ctx);
    if (!upgrade.has_value()) {
      return std::unexpected(upgrade.error());
    }
  }
  TlsGuard tls_guard{tls_ctx, tls_config.enabled};
  const TlsConn conn{socket_fd->get(), tls_config.enabled ? &tls_ctx : nullptr};

  const std::string name = sanitize_file_name(remote_name);
  std::ostringstream header;
  header << "GET /" << name << " HTTP/1.1\r\n";
  header << "Host: " << host << ":" << port << "\r\n";
  if (!access_token.empty()) {
    header << "X-Hyprshare-Token: " << access_token << "\r\n";
  }
  header << "Connection: close\r\n\r\n";

  const std::string header_text = header.str();
  auto send_result = tls_send_all(conn, header_text.data(), header_text.size());
  if (!send_result.has_value()) {
    return std::unexpected(send_result.error());
  }

  std::string response;
  char chunk[8192];
  while (true) {
    const auto received = tls_recv_some(conn, chunk, sizeof(chunk));
    if (!received.has_value()) {
      if (response.empty()) {
        return std::unexpected("failed to read the inbox response: " + received.error());
      }
      break;
    }
    if (*received == 0) {
      break;
    }
    response.append(chunk, static_cast<std::size_t>(*received));
  }

  const auto header_end = response.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    return std::unexpected("the inbox did not answer with a complete HTTP response");
  }
  const std::string status_line = response.substr(0, response.find("\r\n"));
  if (status_line.find("200") == std::string::npos) {
    return std::unexpected("the inbox rejected the download: " + util::trim(status_line));
  }

  const std::string body = response.substr(header_end + 4);
  std::error_code error;
  if (destination.has_parent_path()) {
    std::filesystem::create_directories(destination.parent_path(), error);
  }

  std::ofstream output(destination, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    return std::unexpected("failed to open the destination file: " + destination.string());
  }
  output << body;
  if (output.fail()) {
    return std::unexpected("failed to write the destination file: " + destination.string());
  }

  return body.size();
}

std::expected<InboxArchiveInspection, std::string> inspect_inbox_archive(
  const std::filesystem::path& archive_path,
  const platform::ProcessRunner& runner) {
  if (!runner.command_exists("tar")) {
    return std::unexpected("archive inspection requires `tar`");
  }

  const auto normalized_archive = normalize_existing_archive_path(archive_path);
  if (!normalized_archive.has_value()) {
    return std::unexpected(normalized_archive.error());
  }

  const auto type_listing = runner.run_shell("tar -tvzf " + util::shell_quote(normalized_archive->string()));
  if (type_listing.exit_code != 0) {
    return std::unexpected("failed to inspect archive entry types: " + type_listing.output);
  }
  const auto valid_types = validate_archive_entry_type_listing(type_listing.output);
  if (!valid_types.has_value()) {
    return std::unexpected(valid_types.error());
  }

  const auto listing = runner.run_shell("tar -tzf " + util::shell_quote(normalized_archive->string()));
  if (listing.exit_code != 0) {
    return std::unexpected("failed to list archive contents: " + listing.output);
  }

  return validate_archive_listing(*normalized_archive, listing.output);
}

std::expected<InboxExtractResult, std::string> extract_inbox_archive(
  const std::filesystem::path& archive_path,
  const std::filesystem::path& destination_dir,
  const platform::ProcessRunner& runner,
  const bool dry_run) {
  const auto inspection = inspect_inbox_archive(archive_path, runner);
  if (!inspection.has_value()) {
    return std::unexpected(inspection.error());
  }

  const auto destination = prepare_extract_destination(destination_dir, dry_run);
  if (!destination.has_value()) {
    return std::unexpected(destination.error());
  }

  const auto final_path = unique_child_path(*destination, inspection->top_level);
  if (dry_run) {
    return InboxExtractResult{
      .inspection = *inspection,
      .destination_dir = *destination,
      .extracted_path = final_path,
      .dry_run = true,
    };
  }

  const auto temp_dir = make_extract_temp_dir(*destination);
  if (!temp_dir.has_value()) {
    return std::unexpected(temp_dir.error());
  }

  const auto cleanup = [&temp_dir]() {
    std::error_code remove_error;
    std::filesystem::remove_all(*temp_dir, remove_error);
  };

  const auto extract_result =
    runner.run_shell("tar --no-same-owner --no-same-permissions -xzf " +
                     util::shell_quote(inspection->archive_path.string()) + " -C " + util::shell_quote(temp_dir->string()));
  if (extract_result.exit_code != 0) {
    cleanup();
    return std::unexpected("failed to extract inbox archive: " + extract_result.output);
  }

  const auto extracted_top = *temp_dir / inspection->top_level;
  if (!std::filesystem::exists(extracted_top)) {
    cleanup();
    return std::unexpected("archive did not extract the expected top-level path: " + inspection->top_level);
  }

  const auto valid_tree = validate_extracted_tree(extracted_top);
  if (!valid_tree.has_value()) {
    cleanup();
    return std::unexpected(valid_tree.error());
  }

  std::error_code error;
  std::filesystem::rename(extracted_top, final_path, error);
  if (error) {
    cleanup();
    return std::unexpected("failed to move extracted inbox archive into destination: " + error.message());
  }

  cleanup();
  return InboxExtractResult{
    .inspection = *inspection,
    .destination_dir = *destination,
    .extracted_path = final_path,
    .dry_run = false,
  };
}

}  // namespace hyprshare::core
