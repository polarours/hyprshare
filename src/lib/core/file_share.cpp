#include "hyprshare/core/file_share.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <system_error>
#include <thread>

#include "hyprshare/core/trust_store.hpp"
#include "hyprshare/platform/net.hpp"
#include "hyprshare/tls/tls.hpp"
#include "hyprshare/tls/tls_transport.hpp"
#include "hyprshare/util/string.hpp"

namespace hyprshare::core {

using hyprshare::platform::ScopedFd;
using hyprshare::platform::connect_transport_socket;
using hyprshare::platform::expand_home_path;
using hyprshare::platform::make_ipv4_address;
using hyprshare::platform::send_all;
using hyprshare::platform::set_socket_timeouts;
using hyprshare::tls::TlsContext;
using hyprshare::tls::TlsTransportConfig;

namespace {

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
  const auto received = recv(conn.fd, buf, size, 0);
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

std::expected<std::filesystem::path, std::string> normalize_existing_path(const std::filesystem::path& path) {
  if (path.empty()) {
    return std::unexpected("path must not be empty");
  }

  std::error_code error;
  const auto absolute = std::filesystem::absolute(path, error);
  if (error) {
    return std::unexpected("failed to resolve path `" + path.string() + "`: " + error.message());
  }

  if (!std::filesystem::exists(absolute)) {
    return std::unexpected("path does not exist: " + absolute.string());
  }

  const auto canonical = std::filesystem::weakly_canonical(absolute, error);
  if (error) {
    return std::unexpected("failed to canonicalize path `" + absolute.string() + "`: " + error.message());
  }
  return canonical;
}

std::string relative_display_path(const std::filesystem::path& path) {
  const auto generic = path.generic_string();
  return generic.empty() ? "." : generic;
}

void sort_entries(std::vector<model::FileShareEntry>& entries) {
  std::sort(entries.begin(), entries.end(), [](const model::FileShareEntry& left, const model::FileShareEntry& right) {
    if (left.relative_path == right.relative_path) {
      return left.is_directory && !right.is_directory;
    }
    return left.relative_path < right.relative_path;
  });
}

std::string bool_json(const bool value) { return value ? "true" : "false"; }

std::string string_json(const std::string_view value) { return "\"" + util::escape_json(value) + "\""; }

std::string string_array_json(const std::vector<std::string>& values) {
  std::ostringstream stream;
  stream << "[";
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0) {
      stream << ",";
    }
    stream << string_json(values[index]);
  }
  stream << "]";
  return stream.str();
}

std::string manifest_json(const model::FileShareManifest& manifest) {
  std::ostringstream stream;
  stream << "{"
         << "\"capability\":" << string_json(model::to_string(manifest.capability)) << ","
         << "\"share_name\":" << string_json(manifest.share_name) << ","
         << "\"root_path\":" << string_json(manifest.root_path) << ","
         << "\"read_only\":" << bool_json(manifest.read_only) << ","
         << "\"total_bytes\":" << manifest.total_bytes << ","
         << "\"file_count\":" << manifest.file_count << ","
         << "\"directory_count\":" << manifest.directory_count << ","
         << "\"notes\":" << string_array_json(manifest.notes) << ","
         << "\"entries\":[";

  for (std::size_t index = 0; index < manifest.entries.size(); ++index) {
    const auto& entry = manifest.entries[index];
    if (index > 0) {
      stream << ",";
    }
    stream << "{"
           << "\"relative_path\":" << string_json(entry.relative_path) << ","
           << "\"absolute_path\":" << string_json(entry.absolute_path) << ","
           << "\"is_directory\":" << bool_json(entry.is_directory) << ","
           << "\"size_bytes\":" << entry.size_bytes
           << "}";
  }

  stream << "]}";
  return stream.str();
}

std::string make_session_id() {
  static std::size_t counter = 0;
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  ++counter;
  return "share-" + std::to_string(millis) + "-" + std::to_string(counter);
}

std::expected<void, std::string> write_manifest_file(
  const model::FileShareManifest& manifest,
  const std::filesystem::path& output_path) {
  std::ofstream output(output_path);
  if (!output.is_open()) {
    return std::unexpected("failed to open manifest path for writing: " + output_path.string());
  }
  output << manifest_json(manifest) << "\n";
  return {};
}

std::expected<void, std::string> write_metadata_file(
  const model::FileShareManifest& manifest,
  const std::string_view session_id,
  const std::filesystem::path& output_path) {
  std::ofstream output(output_path);
  if (!output.is_open()) {
    return std::unexpected("failed to open metadata path for writing: " + output_path.string());
  }

  output << "session_id=" << session_id << "\n";
  output << "capability=" << model::to_string(manifest.capability) << "\n";
  output << "share_name=" << manifest.share_name << "\n";
  output << "root_path=" << manifest.root_path << "\n";
  output << "read_only=" << (manifest.read_only ? "true" : "false") << "\n";
  output << "total_bytes=" << manifest.total_bytes << "\n";
  output << "file_count=" << manifest.file_count << "\n";
  output << "directory_count=" << manifest.directory_count << "\n";
  for (const auto& note : manifest.notes) {
    output << "note=" << note << "\n";
  }
  return {};
}

std::expected<void, std::string> copy_payload(
  const model::FileShareManifest& manifest,
  const std::filesystem::path& payload_root,
  std::size_t& copied_file_count,
  std::uintmax_t& copied_bytes) {
  std::error_code error;
  for (const auto& entry : manifest.entries) {
    const auto target = payload_root / entry.relative_path;
    if (entry.is_directory) {
      std::filesystem::create_directories(target, error);
      if (error) {
        return std::unexpected("failed to create staged directory `" + target.string() + "`: " + error.message());
      }
      continue;
    }

    std::filesystem::create_directories(target.parent_path(), error);
    if (error) {
      return std::unexpected("failed to create parent directory for `" + target.string() + "`: " + error.message());
    }

    std::filesystem::copy_file(
      entry.absolute_path,
      target,
      std::filesystem::copy_options::overwrite_existing,
      error);
    if (error) {
      return std::unexpected("failed to stage file `" + entry.absolute_path + "`: " + error.message());
    }

    ++copied_file_count;
    copied_bytes += entry.size_bytes;
  }

  return {};
}

std::expected<void, std::string> write_checksum_file(
  const model::FileShareManifest& manifest,
  const std::filesystem::path& payload_root,
  const std::filesystem::path& checksum_path,
  const platform::ProcessRunner& runner) {
  if (!runner.command_exists("sha256sum")) {
    return std::unexpected("sha256sum is required to build staged payload checksums");
  }

  std::ofstream output(checksum_path);
  if (!output.is_open()) {
    return std::unexpected("failed to open checksum path for writing: " + checksum_path.string());
  }

  for (const auto& entry : manifest.entries) {
    if (entry.is_directory) {
      continue;
    }

    const auto staged_file = payload_root / entry.relative_path;
    const auto result = runner.run_shell("sha256sum " + util::shell_quote(staged_file.string()));
    if (result.exit_code != 0) {
      return std::unexpected("failed to hash staged file `" + staged_file.string() + "`: " + result.output);
    }

    const auto separator = result.output.find("  ");
    if (separator == std::string::npos) {
      return std::unexpected("unexpected sha256sum output for `" + staged_file.string() + "`");
    }

    output << result.output.substr(0, separator) << "  " << entry.relative_path << "\n";
  }

  return {};
}

std::expected<model::FileShareManifest, std::string> load_manifest_from_stage(
  const std::filesystem::path& metadata_path,
  const std::filesystem::path& payload_root) {
  std::ifstream input(metadata_path);
  if (!input.is_open()) {
    return std::unexpected("failed to open staged metadata: " + metadata_path.string());
  }

  model::FileShareManifest manifest;
  std::map<std::string, std::string> values;
  std::string line;
  while (std::getline(input, line)) {
    const auto equals = line.find('=');
    if (equals == std::string::npos) {
      continue;
    }
    const auto key = line.substr(0, equals);
    const auto value = line.substr(equals + 1);
    if (key == "note") {
      manifest.notes.push_back(value);
      continue;
    }
    values.emplace(key, value);
  }

  const auto capability = model::parse_capability(values["capability"]);
  manifest.capability = capability.value_or(model::CapabilityKind::File);
  manifest.share_name = values["share_name"];
  manifest.root_path = values["root_path"];
  manifest.read_only = util::to_lower(values["read_only"]) == "true";
  manifest.total_bytes = values["total_bytes"].empty() ? 0 : static_cast<std::uintmax_t>(std::stoull(values["total_bytes"]));
  manifest.file_count = values["file_count"].empty() ? 0 : static_cast<std::size_t>(std::stoull(values["file_count"]));
  manifest.directory_count =
    values["directory_count"].empty() ? 0 : static_cast<std::size_t>(std::stoull(values["directory_count"]));

  std::error_code error;
  if (std::filesystem::exists(payload_root)) {
    for (std::filesystem::recursive_directory_iterator iterator(payload_root, error), end; iterator != end; iterator.increment(error)) {
      if (error) {
        return std::unexpected("failed to enumerate staged payload `" + payload_root.string() + "`: " + error.message());
      }

      const auto current = iterator->path();
      const auto status = iterator->status(error);
      if (error) {
        return std::unexpected("failed to inspect staged payload path `" + current.string() + "`: " + error.message());
      }

      const auto relative = std::filesystem::relative(current, payload_root, error);
      if (error) {
        return std::unexpected("failed to relativize staged payload path `" + current.string() + "`: " + error.message());
      }

      if (std::filesystem::is_directory(status)) {
        manifest.entries.push_back({
          .relative_path = relative_display_path(relative),
          .absolute_path = current.string(),
          .is_directory = true,
          .size_bytes = 0,
        });
        continue;
      }

      if (std::filesystem::is_regular_file(status)) {
        const auto size = std::filesystem::file_size(current, error);
        if (error) {
          return std::unexpected("failed to read staged file size `" + current.string() + "`: " + error.message());
        }
        manifest.entries.push_back({
          .relative_path = relative_display_path(relative),
          .absolute_path = current.string(),
          .is_directory = false,
          .size_bytes = size,
        });
      }
    }
  }

  sort_entries(manifest.entries);
  return manifest;
}

std::string sanitized_name(std::string name) {
  for (char& ch : name) {
    if (ch == '/' || ch == '\\' || ch == ' ') {
      ch = '_';
    }
  }
  if (name.empty()) {
    name = "share";
  }
  return name;
}

std::expected<void, std::string> copy_directory_tree(
  const std::filesystem::path& source_root,
  const std::filesystem::path& destination_root,
  std::size_t& copied_file_count,
  std::uintmax_t& copied_bytes) {
  std::error_code error;
  std::filesystem::create_directories(destination_root, error);
  if (error) {
    return std::unexpected("failed to create receive directory `" + destination_root.string() + "`: " + error.message());
  }

  for (std::filesystem::recursive_directory_iterator iterator(source_root, error), end; iterator != end; iterator.increment(error)) {
    if (error) {
      return std::unexpected("failed to enumerate staged payload `" + source_root.string() + "`: " + error.message());
    }

    const auto current = iterator->path();
    const auto relative = std::filesystem::relative(current, source_root, error);
    if (error) {
      return std::unexpected("failed to relativize staged payload `" + current.string() + "`: " + error.message());
    }

    const auto target = destination_root / relative;
    const auto status = iterator->status(error);
    if (error) {
      return std::unexpected("failed to inspect staged payload `" + current.string() + "`: " + error.message());
    }

    if (std::filesystem::is_directory(status)) {
      std::filesystem::create_directories(target, error);
      if (error) {
        return std::unexpected("failed to create destination directory `" + target.string() + "`: " + error.message());
      }
      continue;
    }

    if (std::filesystem::is_regular_file(status)) {
      std::filesystem::create_directories(target.parent_path(), error);
      if (error) {
        return std::unexpected("failed to create destination parent `" + target.parent_path().string() + "`: " + error.message());
      }

      std::filesystem::copy_file(current, target, std::filesystem::copy_options::overwrite_existing, error);
      if (error) {
        return std::unexpected("failed to copy staged file `" + current.string() + "`: " + error.message());
      }
      ++copied_file_count;
      copied_bytes += std::filesystem::file_size(current, error);
      if (error) {
        return std::unexpected("failed to read copied file size `" + current.string() + "`: " + error.message());
      }
    }
  }

  return {};
}

std::expected<std::string, std::string> validate_bundle_listing(const std::string_view listing) {
  std::istringstream stream{std::string(listing)};
  std::string line;
  std::string top_level;
  bool saw_entry = false;

  while (std::getline(stream, line)) {
    const std::string entry = util::trim(line);
    if (entry.empty()) {
      continue;
    }

    const std::filesystem::path entry_path(entry);
    if (entry_path.is_absolute()) {
      return std::unexpected("bundle contains absolute archive path: " + entry);
    }

    const auto normalized = entry_path.lexically_normal();
    std::string current_top_level;
    for (const auto& component : normalized) {
      const auto value = component.string();
      if (value.empty() || value == ".") {
        continue;
      }
      if (value == "..") {
        return std::unexpected("bundle contains parent traversal path: " + entry);
      }
      current_top_level = value;
      break;
    }

    if (current_top_level.empty()) {
      return std::unexpected("bundle contains an invalid archive entry: " + entry);
    }

    if (top_level.empty()) {
      top_level = current_top_level;
    } else if (top_level != current_top_level) {
      return std::unexpected("bundle must contain a single top-level session directory");
    }

    saw_entry = true;
  }

  if (!saw_entry) {
    return std::unexpected("bundle archive is empty");
  }

  return top_level;
}

std::int64_t unix_time_now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
           std::chrono::system_clock::now().time_since_epoch())
    .count();
}

std::expected<void, std::string> send_file_contents(
  const TlsConn& conn,
  const std::filesystem::path& file_path,
  std::uintmax_t& bytes_sent) {
  std::ifstream input(file_path, std::ios::binary);
  if (!input.is_open()) {
    return std::unexpected("failed to open bundle for sending: " + file_path.string());
  }

  char buffer[8192];
  while (input.good()) {
    input.read(buffer, static_cast<std::streamsize>(sizeof(buffer)));
    const auto chunk_size = input.gcount();
    if (chunk_size <= 0) {
      break;
    }
    auto send_result = tls_send_all(conn, buffer, static_cast<std::size_t>(chunk_size));
    if (!send_result.has_value()) {
      return std::unexpected(send_result.error());
    }
    bytes_sent += static_cast<std::uintmax_t>(chunk_size);
  }

  if (!input.eof()) {
    return std::unexpected("failed while reading bundle for sending: " + file_path.string());
  }

  return {};
}

std::expected<std::uintmax_t, std::string> file_size_for_transport(const std::filesystem::path& file_path) {
  std::error_code error;
  const auto size = std::filesystem::file_size(file_path, error);
  if (error) {
    return std::unexpected("failed to read file size for `" + file_path.string() + "`: " + error.message());
  }
  return size;
}

std::string offer_header(
  const model::StagedFileShare& staged,
  const std::uintmax_t bundle_size,
  const model::DeviceIdentity& sender_identity,
  const std::string_view allowed_peer_id,
  const std::string_view access_token,
  const std::int64_t auth_expires_at_unix) {
  std::ostringstream stream;
  stream << "HYPRSHARE_OFFER/1\n";
  stream << "Session-Id: " << staged.session_id << "\n";
  stream << "Sender-Device-Id: " << sender_identity.device_id << "\n";
  stream << "Sender-Device-Label: " << sender_identity.device_label << "\n";
  stream << "Allowed-Peer-Id: " << allowed_peer_id << "\n";
  stream << "Capability: " << model::to_string(staged.manifest.capability) << "\n";
  stream << "Share-Name: " << staged.manifest.share_name << "\n";
  stream << "Auth-Required: " << (!access_token.empty() ? "true" : "false") << "\n";
  stream << "Auth-Expires-At: " << auth_expires_at_unix << "\n";
  stream << "Read-Only: " << (staged.manifest.read_only ? "true" : "false") << "\n";
  stream << "Total-Bytes: " << staged.manifest.total_bytes << "\n";
  stream << "File-Count: " << staged.manifest.file_count << "\n";
  stream << "Directory-Count: " << staged.manifest.directory_count << "\n";
  stream << "Bundle-Size: " << bundle_size << "\n";
  for (const auto& note : staged.manifest.notes) {
    stream << "Note: " << note << "\n";
  }
  stream << "\n";
  return stream.str();
}

struct ParsedOffer {
  model::RemoteFileShareOffer offer;
};

struct ParsedRequest {
  std::string command;
  std::string access_token;
  std::string peer_id;
};

std::expected<ParsedOffer, std::string> parse_offer_header(
  const std::string_view header,
  const std::string_view host,
  const std::uint16_t port) {
  std::istringstream stream{std::string(header)};
  std::string line;
  if (!std::getline(stream, line)) {
    return std::unexpected("offer header is empty");
  }

  if (util::trim(line) != "HYPRSHARE_OFFER/1") {
    return std::unexpected("unsupported offer header");
  }

  ParsedOffer parsed;
  parsed.offer.host = std::string(host);
  parsed.offer.port = port;
  try {
    while (std::getline(stream, line)) {
      const auto trimmed = util::trim(line);
      if (trimmed.empty()) {
        continue;
      }

      const auto separator = trimmed.find(':');
      if (separator == std::string::npos) {
        return std::unexpected("invalid offer header line: " + trimmed);
      }

      const auto key = util::trim(trimmed.substr(0, separator));
      const auto value = util::trim(trimmed.substr(separator + 1));
      if (key == "Session-Id") {
        parsed.offer.session_id = value;
      } else if (key == "Sender-Device-Id") {
        parsed.offer.sender_device_id = value;
      } else if (key == "Sender-Device-Label") {
        parsed.offer.sender_device_label = value;
      } else if (key == "Allowed-Peer-Id") {
        parsed.offer.allowed_peer_id = value;
      } else if (key == "Capability") {
        const auto capability = model::parse_capability(value);
        if (!capability.has_value()) {
          return std::unexpected("offer header has an invalid capability: " + value);
        }
        parsed.offer.manifest.capability = *capability;
      } else if (key == "Auth-Required") {
        parsed.offer.auth_required = util::to_lower(value) == "true";
      } else if (key == "Auth-Expires-At") {
        parsed.offer.auth_expires_at_unix = static_cast<std::int64_t>(std::stoll(value));
      } else if (key == "Share-Name") {
        parsed.offer.manifest.share_name = value;
      } else if (key == "Read-Only") {
        parsed.offer.manifest.read_only = util::to_lower(value) == "true";
      } else if (key == "Total-Bytes") {
        parsed.offer.manifest.total_bytes = static_cast<std::uintmax_t>(std::stoull(value));
      } else if (key == "File-Count") {
        parsed.offer.manifest.file_count = static_cast<std::size_t>(std::stoull(value));
      } else if (key == "Directory-Count") {
        parsed.offer.manifest.directory_count = static_cast<std::size_t>(std::stoull(value));
      } else if (key == "Bundle-Size") {
        parsed.offer.bundle_size = static_cast<std::uintmax_t>(std::stoull(value));
      } else if (key == "Note") {
        parsed.offer.manifest.notes.push_back(value);
      }
    }
  } catch (const std::exception&) {
    return std::unexpected("offer header contains an invalid numeric field");
  }

  if (parsed.offer.session_id.empty()) {
    return std::unexpected("offer header is missing Session-Id");
  }
  if (parsed.offer.manifest.share_name.empty()) {
    return std::unexpected("offer header is missing Share-Name");
  }
  if (parsed.offer.bundle_size == 0) {
    return std::unexpected("offer header is missing Bundle-Size");
  }
  return parsed;
}

std::expected<ParsedRequest, std::string> parse_request_line(const std::string_view request_line) {
  const auto trimmed = util::trim(request_line);
  if (trimmed.empty()) {
    return std::unexpected("request line is empty");
  }

  ParsedRequest request;
  std::istringstream stream{std::string(trimmed)};
  if (!(stream >> request.command)) {
    return std::unexpected("request line is empty");
  }

  std::string field;
  while (stream >> field) {
    const auto separator = field.find('=');
    if (separator == std::string::npos) {
      if (!request.access_token.empty()) {
        return std::unexpected("invalid request field: " + field);
      }
      request.access_token = field;
      continue;
    }

    const auto key = field.substr(0, separator);
    const auto value = field.substr(separator + 1);
    if (key == "token") {
      request.access_token = value;
    } else if (key == "peer") {
      request.peer_id = value;
    } else {
      return std::unexpected("unsupported request field: " + field);
    }
  }

  if (request.command.empty()) {
    return std::unexpected("request line is missing a command");
  }
  return request;
}

std::string request_line(
  const std::string_view command,
  const std::string_view access_token,
  const std::string_view peer_id) {
  std::ostringstream stream;
  stream << command;
  if (!access_token.empty()) {
    stream << " token=" << access_token;
  }
  if (!peer_id.empty()) {
    stream << " peer=" << peer_id;
  }
  stream << "\n";
  return stream.str();
}

std::expected<void, std::string> validate_allowed_peer(
  const platform::Paths& paths,
  const std::string_view allowed_peer_id) {
  if (allowed_peer_id.empty()) {
    return {};
  }

  const auto peers = list_trusted_peers(paths);
  if (!peers.has_value()) {
    return std::unexpected(peers.error());
  }

  const auto it = std::find_if(peers->begin(), peers->end(), [allowed_peer_id](const model::TrustedPeer& peer) {
    return peer.device_id == allowed_peer_id;
  });
  if (it == peers->end()) {
    return std::unexpected("allowed peer is not present in the local trust store: " + std::string(allowed_peer_id));
  }
  return {};
}

std::string error_header(const std::string_view message) {
  std::ostringstream stream;
  stream << "HYPRSHARE_ERROR/1\n";
  stream << "Message: " << message << "\n\n";
  return stream.str();
}

std::expected<std::string, std::string> read_request_line(const TlsConn& conn) {
  std::string buffer;
  buffer.reserve(256);
  char chunk[256];
  while (buffer.find('\n') == std::string::npos) {
    const auto received = tls_recv_some(conn, chunk, sizeof(chunk));
    if (!received.has_value()) {
      return std::unexpected("failed while reading request line: " + received.error());
    }
    if (*received == 0) {
      return std::unexpected("client closed before sending a request line");
    }
    buffer.append(chunk, static_cast<std::size_t>(*received));
    if (buffer.size() > 4096) {
      return std::unexpected("request line is too large");
    }
  }

  return buffer.substr(0, buffer.find('\n'));
}

std::expected<std::pair<ParsedOffer, std::string>, std::string> read_offer_from_socket(
  const TlsConn& conn,
  const std::string_view host,
  const std::uint16_t port) {
  std::string buffer;
  buffer.reserve(4096);
  char chunk[4096];
  std::size_t header_end = std::string::npos;
  while ((header_end = buffer.find("\n\n")) == std::string::npos) {
    const auto received = tls_recv_some(conn, chunk, sizeof(chunk));
    if (!received.has_value()) {
      return std::unexpected("failed while reading offer header: " + received.error());
    }
    if (*received == 0) {
      return std::unexpected("remote sender closed before sending a complete offer header");
    }
    buffer.append(chunk, static_cast<std::size_t>(*received));
    if (buffer.size() > 16384) {
      return std::unexpected("offer header is too large");
    }
  }

  const auto header = buffer.substr(0, header_end);
  if (header.starts_with("HYPRSHARE_ERROR/1\n")) {
    const auto message_prefix = std::string("Message:");
    const auto message_position = header.find(message_prefix);
    if (message_position != std::string::npos) {
      return std::unexpected(util::trim(header.substr(message_position + message_prefix.size())));
    }
    return std::unexpected("remote sender returned an error");
  }

  const auto parsed = parse_offer_header(header, host, port);
  if (!parsed.has_value()) {
    return std::unexpected(parsed.error());
  }

  return std::pair<ParsedOffer, std::string>{*parsed, buffer.substr(header_end + 2)};
}

std::expected<std::filesystem::path, std::string> download_bundle_from_socket(
  const TlsConn& conn,
  const model::RemoteFileShareOffer& offer,
  const std::string_view initial_data,
  const std::filesystem::path& output_path) {
  const auto remaining = std::string(initial_data);
  if (remaining.size() > offer.bundle_size) {
    return std::unexpected("received more data than declared in offer header");
  }

  std::ofstream output(output_path, std::ios::binary);
  if (!output.is_open()) {
    return std::unexpected("failed to open downloaded bundle path for writing: " + output_path.string());
  }

  std::uintmax_t written = 0;
  if (!remaining.empty()) {
    output.write(remaining.data(), static_cast<std::streamsize>(remaining.size()));
    written += static_cast<std::uintmax_t>(remaining.size());
  }

  char chunk[4096];
  while (written < offer.bundle_size) {
    const auto max_chunk = static_cast<std::size_t>(std::min<std::uintmax_t>(sizeof(chunk), offer.bundle_size - written));
    const auto received = tls_recv_some(conn, chunk, max_chunk);
    if (!received.has_value()) {
      return std::unexpected("failed while receiving bundle payload: " + received.error());
    }
    if (*received == 0) {
      return std::unexpected("remote sender closed before sending the full bundle payload");
    }
    output.write(chunk, *received);
    written += static_cast<std::uintmax_t>(*received);
  }

  if (!output.good()) {
    return std::unexpected("failed while writing downloaded bundle: " + output_path.string());
  }

  return output_path;
}

}  // namespace

std::expected<model::FileShareManifest, std::string> build_file_manifest(const std::filesystem::path& path) {
  const auto normalized = normalize_existing_path(path);
  if (!normalized.has_value()) {
    return std::unexpected(normalized.error());
  }

  if (!std::filesystem::is_regular_file(*normalized)) {
    return std::unexpected("path is not a regular file: " + normalized->string());
  }

  model::FileShareManifest manifest;
  manifest.capability = model::CapabilityKind::File;
  manifest.share_name = normalized->filename().string();
  manifest.root_path = normalized->string();
  manifest.read_only = true;
  manifest.total_bytes = std::filesystem::file_size(*normalized);
  manifest.file_count = 1;
  manifest.entries.push_back({
    .relative_path = manifest.share_name,
    .absolute_path = normalized->string(),
    .is_directory = false,
    .size_bytes = manifest.total_bytes,
  });
  return manifest;
}

std::expected<model::FileShareManifest, std::string> build_folder_manifest(
  const std::filesystem::path& path,
  const FilesConfig& config) {
  const auto normalized = normalize_existing_path(path);
  if (!normalized.has_value()) {
    return std::unexpected(normalized.error());
  }

  if (!std::filesystem::is_directory(*normalized)) {
    return std::unexpected("path is not a directory: " + normalized->string());
  }

  model::FileShareManifest manifest;
  manifest.capability = model::CapabilityKind::Folder;
  manifest.share_name = normalized->filename().empty() ? normalized->string() : normalized->filename().string();
  manifest.root_path = normalized->string();
  manifest.read_only = config.folder_shares_read_only_by_default;

  std::error_code error;
  for (std::filesystem::recursive_directory_iterator iterator(*normalized, error), end; iterator != end; iterator.increment(error)) {
    if (error) {
      return std::unexpected("failed to enumerate folder `" + normalized->string() + "`: " + error.message());
    }

    const auto current = iterator->path();
    const auto status = iterator->symlink_status(error);
    if (error) {
      return std::unexpected("failed to inspect path `" + current.string() + "`: " + error.message());
    }

    if (std::filesystem::is_symlink(status)) {
      return std::unexpected("folder share does not follow symbolic links: " + current.string());
    }

    const auto relative = std::filesystem::relative(current, *normalized, error);
    if (error) {
      return std::unexpected("failed to relativize path `" + current.string() + "`: " + error.message());
    }

    if (std::filesystem::is_directory(status)) {
      ++manifest.directory_count;
      manifest.entries.push_back({
        .relative_path = relative_display_path(relative),
        .absolute_path = current.string(),
        .is_directory = true,
        .size_bytes = 0,
      });
      continue;
    }

    if (!std::filesystem::is_regular_file(status)) {
      return std::unexpected("folder share only supports regular files and directories: " + current.string());
    }

    const auto size = std::filesystem::file_size(current, error);
    if (error) {
      return std::unexpected("failed to read file size for `" + current.string() + "`: " + error.message());
    }

    ++manifest.file_count;
    manifest.total_bytes += size;
    manifest.entries.push_back({
      .relative_path = relative_display_path(relative),
      .absolute_path = current.string(),
      .is_directory = false,
      .size_bytes = size,
    });
  }

  sort_entries(manifest.entries);
  if (manifest.read_only) {
    manifest.notes.push_back("Folder shares default to read-only according to current file-sharing config.");
  }
  return manifest;
}

std::expected<model::StagedFileShare, std::string> stage_file_share(
  const model::FileShareManifest& manifest,
  const platform::Paths& paths,
  const platform::ProcessRunner& runner) {
  if (manifest.entries.empty()) {
    return std::unexpected("cannot stage an empty file share manifest");
  }

  std::error_code error;
  const auto share_root = paths.state_dir / "shares" / make_session_id();
  const auto payload_root = share_root / "payload";
  const auto manifest_path = share_root / "manifest.json";
  const auto metadata_path = share_root / "session.meta";
  const auto checksum_path = share_root / "payload.sha256";

  std::filesystem::create_directories(payload_root, error);
  if (error) {
    return std::unexpected("failed to create staged share directory `" + payload_root.string() + "`: " + error.message());
  }

  auto manifest_result = write_manifest_file(manifest, manifest_path);
  if (!manifest_result.has_value()) {
    return std::unexpected(manifest_result.error());
  }

  auto metadata_result = write_metadata_file(manifest, share_root.filename().string(), metadata_path);
  if (!metadata_result.has_value()) {
    return std::unexpected(metadata_result.error());
  }

  std::size_t copied_file_count = 0;
  std::uintmax_t copied_bytes = 0;
  auto copy_result = copy_payload(manifest, payload_root, copied_file_count, copied_bytes);
  if (!copy_result.has_value()) {
    return std::unexpected(copy_result.error());
  }

  auto checksum_result = write_checksum_file(manifest, payload_root, checksum_path, runner);
  if (!checksum_result.has_value()) {
    return std::unexpected(checksum_result.error());
  }

  model::StagedFileShare staged;
  staged.session_id = share_root.filename().string();
  staged.stage_path = share_root.string();
  staged.manifest_path = manifest_path.string();
  staged.metadata_path = metadata_path.string();
  staged.payload_path = payload_root.string();
  staged.checksum_path = checksum_path.string();
  staged.copied_file_count = copied_file_count;
  staged.copied_bytes = copied_bytes;
  staged.manifest = manifest;
  return staged;
}

std::expected<model::StagedFileShare, std::string> load_staged_file_share(
  const std::string_view session_id,
  const platform::Paths& paths) {
  const auto share_root = paths.state_dir / "shares" / std::string(session_id);
  const auto metadata_path = share_root / "session.meta";
  const auto manifest_path = share_root / "manifest.json";
  const auto payload_path = share_root / "payload";
  const auto checksum_path = share_root / "payload.sha256";

  if (!std::filesystem::exists(share_root)) {
    return std::unexpected("staged share session does not exist: " + share_root.string());
  }

  auto manifest = load_manifest_from_stage(metadata_path, payload_path);
  if (!manifest.has_value()) {
    return std::unexpected(manifest.error());
  }

  model::StagedFileShare staged;
  staged.session_id = std::string(session_id);
  staged.stage_path = share_root.string();
  staged.manifest_path = manifest_path.string();
  staged.metadata_path = metadata_path.string();
  staged.payload_path = payload_path.string();
  staged.checksum_path = std::filesystem::exists(checksum_path) ? checksum_path.string() : "";
  staged.copied_file_count = manifest->file_count;
  staged.copied_bytes = manifest->total_bytes;
  staged.manifest = *manifest;
  return staged;
}

std::expected<std::filesystem::path, std::string> delete_staged_file_share(
  const std::string_view session_id,
  const platform::Paths& paths) {
  const auto share_root = paths.state_dir / "shares" / std::string(session_id);
  if (!std::filesystem::exists(share_root)) {
    return std::unexpected("staged share session does not exist: " + share_root.string());
  }
  if (!std::filesystem::is_directory(share_root)) {
    return std::unexpected("staged share session path is not a directory: " + share_root.string());
  }

  std::error_code error;
  std::filesystem::remove_all(share_root, error);
  if (error) {
    return std::unexpected("failed to delete staged share session `" + share_root.string() + "`: " + error.message());
  }
  return share_root;
}

std::expected<std::vector<std::filesystem::path>, std::string> prune_staged_file_shares(
  const platform::Paths& paths,
  const std::optional<std::uint32_t> older_than_seconds) {
  const auto share_root = paths.state_dir / "shares";
  if (!std::filesystem::exists(share_root)) {
    return std::vector<std::filesystem::path>{};
  }

  std::vector<std::filesystem::path> removed;
  const auto now = std::filesystem::file_time_type::clock::now();
  const auto cutoff =
    older_than_seconds.has_value() ? now - std::chrono::seconds(*older_than_seconds) : std::filesystem::file_time_type::min();

  for (const auto& entry : std::filesystem::directory_iterator(share_root)) {
    if (!entry.is_directory()) {
      continue;
    }

    if (older_than_seconds.has_value()) {
      std::error_code error;
      const auto last_write = std::filesystem::last_write_time(entry.path(), error);
      if (error) {
        return std::unexpected(
          "failed to read staged share timestamp `" + entry.path().string() + "`: " + error.message());
      }
      if (last_write > cutoff) {
        continue;
      }
    }

    std::error_code error;
    std::filesystem::remove_all(entry.path(), error);
    if (error) {
      return std::unexpected("failed to prune staged share session `" + entry.path().string() + "`: " + error.message());
    }
    removed.push_back(entry.path());
  }

  std::sort(removed.begin(), removed.end(), [](const auto& left, const auto& right) {
    return left.filename().string() > right.filename().string();
  });
  return removed;
}

std::expected<void, std::string> verify_staged_file_share(
  const model::StagedFileShare& staged,
  const platform::ProcessRunner& runner) {
  if (!runner.command_exists("sha256sum")) {
    return std::unexpected("sha256sum is required to verify staged payload checksums");
  }

  if (staged.checksum_path.empty()) {
    return std::unexpected("staged share does not have a checksum file");
  }

  if (!std::filesystem::exists(staged.checksum_path)) {
    return std::unexpected("staged share checksum file does not exist: " + staged.checksum_path);
  }

  const auto payload_root = std::filesystem::path(staged.payload_path);
  const auto checksum_path = std::filesystem::path(staged.checksum_path);
  const auto result = runner.run_shell("cd " + util::shell_quote(payload_root.string()) + " && sha256sum -c " +
                                       util::shell_quote(std::filesystem::relative(checksum_path, payload_root).string()));
  if (result.exit_code != 0) {
    return std::unexpected("staged share verification failed: " + result.output);
  }

  return {};
}

std::expected<model::RemoteFileShareOffer, std::string> inspect_remote_file_share(
  const std::string_view host,
  const std::uint16_t port,
  const std::string_view access_token,
  const std::string_view peer_id,
  const TlsTransportConfig& tls_config) {
  auto socket_fd = connect_transport_socket(host, port);
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

  const std::string request = request_line("INSPECT", access_token, peer_id);
  auto send_result = tls_send_all(conn, request.data(), request.size());
  if (!send_result.has_value()) {
    return std::unexpected(send_result.error());
  }

  const auto offer = read_offer_from_socket(conn, host, port);
  if (!offer.has_value()) {
    return std::unexpected(offer.error());
  }

  return offer->first.offer;
}

std::expected<void, std::string> probe_remote_file_share(
  const std::string_view host,
  const std::uint16_t port,
  const std::string_view access_token,
  const std::string_view peer_id,
  const TlsTransportConfig& tls_config) {
  auto socket_fd = connect_transport_socket(host, port);
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

  const std::string request = request_line("PROBE", access_token, peer_id);
  auto send_result = tls_send_all(conn, request.data(), request.size());
  if (!send_result.has_value()) {
    return std::unexpected(send_result.error());
  }

  fd_set read_set;
  FD_ZERO(&read_set);
  FD_SET(socket_fd->get(), &read_set);

  timeval timeout{};
  timeout.tv_sec = 0;
  timeout.tv_usec = 200000;
  const auto ready = select(socket_fd->get() + 1, &read_set, nullptr, nullptr, &timeout);
  if (ready > 0) {
    char buffer[256];
    static_cast<void>(tls_recv_some(conn, buffer, sizeof(buffer)));
  }

  return {};
}

std::expected<std::uintmax_t, std::string> serve_staged_file_share(
  const model::StagedFileShare& staged,
  const std::string_view bind_host,
  const std::uint16_t port,
  const platform::Paths& paths,
  const platform::ProcessRunner& runner,
  const std::string_view access_token,
  const std::uint32_t token_ttl_seconds,
  const std::string_view allowed_peer_id,
  const std::uint32_t idle_timeout_seconds,
  const TlsTransportConfig& tls_config) {
  if (idle_timeout_seconds == 0) {
    return std::unexpected("transfer timeout must be greater than zero");
  }

  auto verification = verify_staged_file_share(staged, runner);
  if (!verification.has_value()) {
    return std::unexpected(verification.error());
  }

  const auto allowed_peer_validation = validate_allowed_peer(paths, allowed_peer_id);
  if (!allowed_peer_validation.has_value()) {
    return std::unexpected(allowed_peer_validation.error());
  }

  const auto sender_identity = load_or_create_device_identity(paths);
  if (!sender_identity.has_value()) {
    return std::unexpected(sender_identity.error());
  }

  const auto transport_root = paths.state_dir / "transport";
  const auto bundle_path = transport_root / (staged.session_id + "-" + make_session_id() + ".tar.gz");
  const auto cleanup = [&bundle_path]() {
    std::error_code remove_error;
    std::filesystem::remove(bundle_path, remove_error);
  };

  std::error_code error;
  std::filesystem::create_directories(transport_root, error);
  if (error) {
    return std::unexpected("failed to create transport directory `" + transport_root.string() + "`: " + error.message());
  }

  const auto exported = export_staged_file_share(staged, bundle_path, runner);
  if (!exported.has_value()) {
    cleanup();
    return std::unexpected(exported.error());
  }

  const auto bundle_size = file_size_for_transport(*exported);
  if (!bundle_size.has_value()) {
    cleanup();
    return std::unexpected(bundle_size.error());
  }

  const auto address = make_ipv4_address(bind_host, port);
  if (!address.has_value()) {
    cleanup();
    return std::unexpected(address.error());
  }

  ScopedFd listen_fd(socket(AF_INET, SOCK_STREAM, 0));
  if (!listen_fd.valid()) {
    cleanup();
    return std::unexpected("failed to create server socket: " + std::string(std::strerror(errno)));
  }

  const int reuse_addr = 1;
  if (setsockopt(listen_fd.get(), SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) != 0) {
    cleanup();
    return std::unexpected("failed to configure server socket: " + std::string(std::strerror(errno)));
  }

  if (bind(listen_fd.get(), reinterpret_cast<const sockaddr*>(&*address), sizeof(*address)) != 0) {
    cleanup();
    return std::unexpected("failed to bind transfer socket: " + std::string(std::strerror(errno)));
  }

  if (listen(listen_fd.get(), 1) != 0) {
    cleanup();
    return std::unexpected("failed to listen for transfer client: " + std::string(std::strerror(errno)));
  }

  const bool has_auth_deadline = !access_token.empty() && token_ttl_seconds > 0;
  const auto auth_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(token_ttl_seconds);
  const auto auth_expires_at_unix =
    has_auth_deadline ? (unix_time_now() + static_cast<std::int64_t>(token_ttl_seconds)) : 0;
  const auto offer = offer_header(staged, *bundle_size, *sender_identity, allowed_peer_id, access_token, auth_expires_at_unix);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(idle_timeout_seconds);
  const bool auth_before_session_deadline = has_auth_deadline && auth_deadline <= deadline;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto now = std::chrono::steady_clock::now();
    if (has_auth_deadline && now >= auth_deadline) {
      cleanup();
      return std::unexpected("access token expired before an authorized fetch completed");
    }

    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(listen_fd.get(), &read_set);

    const auto session_remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    auto remaining = session_remaining;
    if (has_auth_deadline) {
      const auto auth_remaining = std::chrono::duration_cast<std::chrono::milliseconds>(auth_deadline - now);
      remaining = std::min(session_remaining, auth_remaining);
      if (remaining <= std::chrono::milliseconds::zero()) {
        cleanup();
        return std::unexpected("access token expired before an authorized fetch completed");
      }
    }
    timeval timeout{};
    timeout.tv_sec = static_cast<long>(remaining.count() / 1000);
    timeout.tv_usec = static_cast<long>((remaining.count() % 1000) * 1000);
    const auto ready = select(listen_fd.get() + 1, &read_set, nullptr, nullptr, &timeout);
    if (ready < 0) {
      cleanup();
      return std::unexpected("failed while waiting for transfer client: " + std::string(std::strerror(errno)));
    }
    if (ready == 0) {
      if (auth_before_session_deadline) {
        cleanup();
        return std::unexpected("access token expired before an authorized fetch completed");
      }
      break;
    }

    sockaddr_in client_address{};
    socklen_t client_address_length = sizeof(client_address);
    ScopedFd client_fd(accept(listen_fd.get(), reinterpret_cast<sockaddr*>(&client_address), &client_address_length));
    if (!client_fd.valid()) {
      cleanup();
      return std::unexpected("failed to accept transfer client: " + std::string(std::strerror(errno)));
    }

    auto timeout_result = set_socket_timeouts(client_fd.get());
    if (!timeout_result.has_value()) {
      cleanup();
      return std::unexpected(timeout_result.error());
    }

    TlsContext tls_ctx{};
    if (tls_config.enabled) {
      auto upgrade = hyprshare::tls::upgrade_server_socket(client_fd.get(), tls_config, tls_ctx);
      if (!upgrade.has_value()) {
        cleanup();
        return std::unexpected(upgrade.error());
      }
    }
    TlsGuard tls_guard{tls_ctx, tls_config.enabled};
    const TlsConn conn{client_fd.get(), tls_config.enabled ? &tls_ctx : nullptr};

    const auto request_line = read_request_line(conn);
    if (!request_line.has_value()) {
      cleanup();
      return std::unexpected(request_line.error());
    }

    const auto request = parse_request_line(*request_line);
    if (!request.has_value()) {
      const auto response = error_header(request.error());
      static_cast<void>(tls_send_all(conn, response.data(), response.size()));
      continue;
    }

    if (has_auth_deadline && std::chrono::steady_clock::now() >= auth_deadline) {
      const auto response = error_header("access token expired");
      static_cast<void>(tls_send_all(conn, response.data(), response.size()));
      continue;
    }

    if (!access_token.empty() && request->access_token != access_token) {
      const auto response = error_header("access token required or invalid");
      static_cast<void>(tls_send_all(conn, response.data(), response.size()));
      continue;
    }

    if (!allowed_peer_id.empty() && request->peer_id != allowed_peer_id) {
      const auto response = error_header("trusted peer identity required or invalid");
      static_cast<void>(tls_send_all(conn, response.data(), response.size()));
      continue;
    }

    if (request->command != "INSPECT" && request->command != "FETCH") {
      const auto response = error_header("unsupported request command");
      static_cast<void>(tls_send_all(conn, response.data(), response.size()));
      continue;
    }

    auto header_result = tls_send_all(conn, offer.data(), offer.size());
    if (!header_result.has_value()) {
      cleanup();
      return std::unexpected(header_result.error());
    }

    if (request->command == "INSPECT") {
      continue;
    }

    std::uintmax_t bytes_sent = 0;
    auto send_result = send_file_contents(conn, *exported, bytes_sent);
    cleanup();
    if (!send_result.has_value()) {
      return std::unexpected(send_result.error());
    }
    return bytes_sent;
  }

  cleanup();
  return std::unexpected("timed out waiting for a transfer client");
}

std::expected<model::StagedFileShare, std::string> fetch_staged_file_share(
  const std::string_view host,
  const std::uint16_t port,
  const platform::Paths& paths,
  const platform::ProcessRunner& runner,
  const std::string_view access_token,
  const std::string_view peer_id,
  const TlsTransportConfig& tls_config) {
  const auto transport_root = paths.state_dir / "transport";
  const auto bundle_path = transport_root / ("fetch-" + make_session_id() + ".tar.gz");
  const auto cleanup = [&bundle_path]() {
    std::error_code remove_error;
    std::filesystem::remove(bundle_path, remove_error);
  };

  std::error_code error;
  std::filesystem::create_directories(transport_root, error);
  if (error) {
    return std::unexpected("failed to create transport directory `" + transport_root.string() + "`: " + error.message());
  }

  auto socket_fd = connect_transport_socket(host, port);
  if (!socket_fd.has_value()) {
    cleanup();
    return std::unexpected(socket_fd.error());
  }

  TlsContext tls_ctx{};
  if (tls_config.enabled) {
    auto upgrade = hyprshare::tls::upgrade_client_socket(socket_fd->get(), tls_config, tls_ctx);
    if (!upgrade.has_value()) {
      cleanup();
      return std::unexpected(upgrade.error());
    }
  }
  TlsGuard tls_guard{tls_ctx, tls_config.enabled};
  const TlsConn conn{socket_fd->get(), tls_config.enabled ? &tls_ctx : nullptr};

  const std::string request = request_line("FETCH", access_token, peer_id);
  auto send_result = tls_send_all(conn, request.data(), request.size());
  if (!send_result.has_value()) {
    cleanup();
    return std::unexpected(send_result.error());
  }

  const auto offer = read_offer_from_socket(conn, host, port);
  if (!offer.has_value()) {
    cleanup();
    return std::unexpected(offer.error());
  }

  const auto downloaded = download_bundle_from_socket(conn, offer->first.offer, offer->second, bundle_path);
  if (!downloaded.has_value()) {
    cleanup();
    return std::unexpected(downloaded.error());
  }

  const auto imported = import_staged_file_share(*downloaded, paths, runner);
  cleanup();
  if (!imported.has_value()) {
    return std::unexpected(imported.error());
  }

  return *imported;
}

std::expected<std::filesystem::path, std::string> export_staged_file_share(
  const model::StagedFileShare& staged,
  const std::filesystem::path& bundle_path,
  const platform::ProcessRunner& runner) {
  if (!runner.command_exists("tar")) {
    return std::unexpected("tar is required to export a staged share bundle");
  }

  const auto stage_root = std::filesystem::path(staged.stage_path);
  if (!std::filesystem::exists(stage_root)) {
    return std::unexpected("staged share session does not exist: " + stage_root.string());
  }

  const auto expanded_bundle = expand_home_path(bundle_path);
  if (expanded_bundle.empty()) {
    return std::unexpected("bundle output path must not be empty");
  }

  std::error_code error;
  const auto absolute_bundle = std::filesystem::absolute(expanded_bundle, error);
  if (error) {
    return std::unexpected("failed to resolve bundle output path `" + expanded_bundle.string() + "`: " + error.message());
  }

  std::filesystem::create_directories(absolute_bundle.parent_path(), error);
  if (error) {
    return std::unexpected("failed to create bundle parent directory `" + absolute_bundle.parent_path().string() + "`: " +
                           error.message());
  }

  const std::string command = "tar -czf " + util::shell_quote(absolute_bundle.string()) + " -C " +
                              util::shell_quote(stage_root.parent_path().string()) + " " +
                              util::shell_quote(stage_root.filename().string());
  const auto result = runner.run_shell(command);
  if (result.exit_code != 0) {
    return std::unexpected("failed to export staged share bundle: " + result.output);
  }

  return absolute_bundle;
}

std::expected<model::StagedFileShare, std::string> import_staged_file_share(
  const std::filesystem::path& bundle_path,
  const platform::Paths& paths,
  const platform::ProcessRunner& runner) {
  if (!runner.command_exists("tar")) {
    return std::unexpected("tar is required to import a staged share bundle");
  }

  const auto normalized_bundle = normalize_existing_path(bundle_path);
  if (!normalized_bundle.has_value()) {
    return std::unexpected(normalized_bundle.error());
  }

  if (!std::filesystem::is_regular_file(*normalized_bundle)) {
    return std::unexpected("bundle path is not a regular file: " + normalized_bundle->string());
  }

  const auto listing_result = runner.run_shell("tar -tzf " + util::shell_quote(normalized_bundle->string()));
  if (listing_result.exit_code != 0) {
    return std::unexpected("failed to inspect bundle archive: " + listing_result.output);
  }

  const auto top_level = validate_bundle_listing(listing_result.output);
  if (!top_level.has_value()) {
    return std::unexpected(top_level.error());
  }

  const auto shares_root = paths.state_dir / "shares";
  const auto extract_root = paths.state_dir / "share-import" / make_session_id();
  const auto cleanup = [&extract_root]() {
    std::error_code remove_error;
    std::filesystem::remove_all(extract_root, remove_error);
  };

  std::error_code error;
  std::filesystem::create_directories(extract_root, error);
  if (error) {
    return std::unexpected("failed to create bundle extraction directory `" + extract_root.string() + "`: " + error.message());
  }

  const auto extract_result =
    runner.run_shell("tar -xzf " + util::shell_quote(normalized_bundle->string()) + " -C " + util::shell_quote(extract_root.string()));
  if (extract_result.exit_code != 0) {
    cleanup();
    return std::unexpected("failed to extract bundle archive: " + extract_result.output);
  }

  const auto extracted_stage_root = extract_root / *top_level;
  if (!std::filesystem::exists(extracted_stage_root) || !std::filesystem::is_directory(extracted_stage_root)) {
    cleanup();
    return std::unexpected("bundle did not contain a staged share directory");
  }

  auto manifest = load_manifest_from_stage(extracted_stage_root / "session.meta", extracted_stage_root / "payload");
  if (!manifest.has_value()) {
    cleanup();
    return std::unexpected(manifest.error());
  }

  model::StagedFileShare extracted;
  extracted.session_id = extracted_stage_root.filename().string();
  extracted.stage_path = extracted_stage_root.string();
  extracted.manifest_path = (extracted_stage_root / "manifest.json").string();
  extracted.metadata_path = (extracted_stage_root / "session.meta").string();
  extracted.payload_path = (extracted_stage_root / "payload").string();
  extracted.checksum_path =
    std::filesystem::exists(extracted_stage_root / "payload.sha256") ? (extracted_stage_root / "payload.sha256").string() : "";
  extracted.copied_file_count = manifest->file_count;
  extracted.copied_bytes = manifest->total_bytes;
  extracted.manifest = *manifest;

  if (!extracted.checksum_path.empty()) {
    auto verification = verify_staged_file_share(extracted, runner);
    if (!verification.has_value()) {
      cleanup();
      return std::unexpected(verification.error());
    }
  }

  std::filesystem::create_directories(shares_root, error);
  if (error) {
    cleanup();
    return std::unexpected("failed to create staged share root `" + shares_root.string() + "`: " + error.message());
  }

  const auto imported_session_id = make_session_id();
  const auto imported_stage_root = shares_root / imported_session_id;
  std::filesystem::rename(extracted_stage_root, imported_stage_root, error);
  if (error) {
    cleanup();
    return std::unexpected("failed to move imported staged share into state directory: " + error.message());
  }

  const auto metadata_result = write_metadata_file(*manifest, imported_session_id, imported_stage_root / "session.meta");
  if (!metadata_result.has_value()) {
    cleanup();
    return std::unexpected(metadata_result.error());
  }

  cleanup();
  return load_staged_file_share(imported_session_id, paths);
}

std::expected<std::vector<model::StagedFileShare>, std::string> list_staged_file_shares(
  const platform::Paths& paths) {
  const auto share_root = paths.state_dir / "shares";
  if (!std::filesystem::exists(share_root)) {
    return std::vector<model::StagedFileShare>{};
  }

  std::vector<model::StagedFileShare> staged;
  for (const auto& entry : std::filesystem::directory_iterator(share_root)) {
    if (!entry.is_directory()) {
      continue;
    }

    auto session = load_staged_file_share(entry.path().filename().string(), paths);
    if (!session.has_value()) {
      return std::unexpected(session.error());
    }
    staged.push_back(*session);
  }

  std::sort(staged.begin(), staged.end(), [](const model::StagedFileShare& left, const model::StagedFileShare& right) {
    return left.session_id > right.session_id;
  });
  return staged;
}

std::expected<model::ReceivedFileShare, std::string> receive_staged_file_share(
  const model::StagedFileShare& staged,
  const std::filesystem::path& destination_base) {
  const auto expanded_base = expand_home_path(destination_base);
  const auto destination =
    expanded_base / (staged.session_id + "-" + sanitized_name(staged.manifest.share_name));

  if (std::filesystem::exists(destination)) {
    return std::unexpected("receive destination already exists: " + destination.string());
  }

  std::size_t copied_file_count = 0;
  std::uintmax_t copied_bytes = 0;
  auto copy_result = copy_directory_tree(staged.payload_path, destination, copied_file_count, copied_bytes);
  if (!copy_result.has_value()) {
    return std::unexpected(copy_result.error());
  }

  model::ReceivedFileShare received;
  received.session_id = staged.session_id;
  received.destination_path = destination.string();
  received.copied_file_count = copied_file_count;
  received.copied_bytes = copied_bytes;
  received.manifest = staged.manifest;
  return received;
}

}  // namespace hyprshare::core
