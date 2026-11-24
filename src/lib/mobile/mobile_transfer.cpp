#include "hyprshare/platform/net.hpp"
#include "hyprshare/platform/process.hpp"
#include "hyprshare/mobile/mobile_transfer.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/evp.h>

#include <array>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

namespace hyprshare::mobile {

namespace {

bool send_all(int fd, const void* data, std::size_t size) {
  const auto* bytes = static_cast<const char*>(data);
  std::size_t sent = 0;
  while (sent < size) {
    const auto n = send(fd, bytes + sent, size - sent, 0);
    if (n <= 0) {
      return false;
    }
    sent += static_cast<std::size_t>(n);
  }
  return true;
}

std::string extract_quoted(const std::string& payload, const std::string& key) {
  const auto tag = "\"" + key + "\":";
  const auto pos = payload.find(tag);
  if (pos == std::string::npos) {
    return "";
  }
  const auto start = payload.find('"', pos + tag.size());
  if (start == std::string::npos) {
    return "";
  }
  const auto end = payload.find('"', start + 1);
  if (end == std::string::npos) {
    return "";
  }
  return payload.substr(start + 1, end - start - 1);
}

std::uint64_t extract_size(const std::string& payload) {
  const auto pos = payload.find("\"size\":");
  if (pos == std::string::npos) {
    return 0;
  }
  std::string number;
  for (std::size_t i = pos + 7; i < payload.size() && std::isdigit(static_cast<unsigned char>(payload[i])); ++i) {
    number.push_back(payload[i]);
  }
  return number.empty() ? 0 : std::strtoull(number.c_str(), nullptr, 10);
}

std::filesystem::path inbox_dir() {
  const char* home = std::getenv("HOME");
  auto base = std::filesystem::path(home ? home : "/");
  base /= ".local/share/hyprshare/inbox";
  std::error_code ec;
  std::filesystem::create_directories(base, ec);
  return base;
}

std::string json_escape(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (const char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out.push_back(c); break;
    }
  }
  return out;
}

// Collects every quoted string inside the JSON array that follows `key`, so a
// client can report `"capabilities":["file_transfer","clipboard"]`.
std::vector<std::string> extract_string_array(const std::string& payload, const std::string& key) {
  std::vector<std::string> values;
  const auto key_pos = payload.find("\"" + key + "\"");
  if (key_pos == std::string::npos) {
    return values;
  }
  const auto open = payload.find('[', key_pos);
  if (open == std::string::npos) {
    return values;
  }
  const auto close = payload.find(']', open);
  if (close == std::string::npos) {
    return values;
  }
  const auto body = payload.substr(open + 1, close - open - 1);
  std::size_t pos = 0;
  while ((pos = body.find('"', pos)) != std::string::npos) {
    const auto end = body.find('"', pos + 1);
    if (end == std::string::npos) {
      break;
    }
    values.push_back(body.substr(pos + 1, end - pos - 1));
    pos = end + 1;
  }
  return values;
}

// The address advertised in `discover_response` must be reachable from the
// phone, so report the host's real LAN IPv4 instead of a wildcard bind address.
std::string local_ipv4() {
  struct ifaddrs* addresses = nullptr;
  if (getifaddrs(&addresses) != 0) {
    return "127.0.0.1";
  }
  std::string fallback;
  std::string chosen;
  for (auto* current = addresses; current != nullptr; current = current->ifa_next) {
    if (current->ifa_addr == nullptr || current->ifa_addr->sa_family != AF_INET) {
      continue;
    }
    if ((current->ifa_flags & IFF_LOOPBACK) != 0 || (current->ifa_flags & IFF_UP) == 0) {
      continue;
    }
    char buffer[INET_ADDRSTRLEN] = {};
    const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(current->ifa_addr);
    if (inet_ntop(AF_INET, &ipv4->sin_addr, buffer, sizeof(buffer)) == nullptr) {
      continue;
    }
    const std::string address = buffer;
    if (fallback.empty()) {
      fallback = address;
    }
    // Prefer a routable private/public address over the IPv4 link-local range.
    if (address.rfind("169.254.", 0) != 0) {
      chosen = address;
      break;
    }
  }
  freeifaddrs(addresses);
  return chosen.empty() ? (fallback.empty() ? "127.0.0.1" : fallback) : chosen;
}

std::expected<std::string, std::string> file_sha256(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return std::unexpected("failed to open file for hashing: " + path.string());
  }
  EVP_MD_CTX* context = EVP_MD_CTX_new();
  if (context == nullptr) {
    return std::unexpected("failed to create the hashing context");
  }
  if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1) {
    EVP_MD_CTX_free(context);
    return std::unexpected("failed to initialise SHA-256");
  }
  std::array<char, 65536> buffer{};
  while (input.good()) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = input.gcount();
    if (count > 0 && EVP_DigestUpdate(context, buffer.data(), static_cast<std::size_t>(count)) != 1) {
      EVP_MD_CTX_free(context);
      return std::unexpected("failed to hash file contents");
    }
  }
  unsigned char digest[EVP_MAX_MD_SIZE] = {};
  unsigned int length = 0;
  if (EVP_DigestFinal_ex(context, digest, &length) != 1) {
    EVP_MD_CTX_free(context);
    return std::unexpected("failed to finalise SHA-256");
  }
  EVP_MD_CTX_free(context);
  std::string hex;
  hex.reserve(static_cast<std::size_t>(length) * 2);
  static constexpr char kDigits[] = "0123456789abcdef";
  for (unsigned int i = 0; i < length; ++i) {
    hex.push_back(kDigits[digest[i] >> 4]);
    hex.push_back(kDigits[digest[i] & 0x0F]);
  }
  return hex;
}

std::string now_millis() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

}  // namespace

MobileTransferServer::MobileTransferServer(const uint16_t port) : port_(port) {}

MobileTransferServer::~MobileTransferServer() {
  stop();
}

std::expected<void, std::string> MobileTransferServer::start() {
  server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd_ < 0) {
    return std::unexpected("failed to create socket");
  }

  int reuse = 1;
  setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port_);

  if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    close(server_fd_);
    return std::unexpected("failed to bind to port " + std::to_string(port_));
  }

  if (listen(server_fd_, 5) != 0) {
    close(server_fd_);
    return std::unexpected("failed to listen");
  }

  running_ = true;
  accept_thread_ = std::thread(&MobileTransferServer::accept_loop, this);

  return {};
}

void MobileTransferServer::stop() {
  running_ = false;
  if (server_fd_ >= 0) {
    // shutdown() wakes up the thread that is waiting in accept(), so that it
    // leaves the loop instead of spinning on a closed descriptor.
    ::shutdown(server_fd_, SHUT_RDWR);
  }
  // The thread has to be finished before the descriptor is closed, otherwise
  // the number can be handed to another socket while the thread still waits
  // on it.
  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }
  if (server_fd_ >= 0) {
    close(server_fd_);
    server_fd_ = -1;
  }
  {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto& client : clients_) {
      if (client.fd >= 0) {
        close(client.fd);
        client.fd = -1;
      }
    }
    clients_.clear();
  }
}

bool MobileTransferServer::is_running() const {
  return running_;
}

void MobileTransferServer::set_transfer_callback(TransferCallback callback) {
  callback_ = std::move(callback);
}

std::expected<void, std::string> MobileTransferServer::send_file(
  const std::string& device_id,
  const std::filesystem::path& file_path) {
  if (!std::filesystem::exists(file_path) || !std::filesystem::is_regular_file(file_path)) {
    return std::unexpected("file not found: " + file_path.string());
  }

  // Snapshot the matching descriptors under the lock, then stream without it:
  // holding the mutex across socket writes would block accept/auth handling.
  std::vector<int> targets;
  {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (const auto& client : clients_) {
      if (client.device_id == device_id && client.fd >= 0) {
        targets.push_back(client.fd);
      }
    }
  }

  for (const int client_fd : targets) {
    FileTransfer transfer;
    transfer.name = file_path.filename().string();
    transfer.local_path = file_path;
    transfer.size = std::filesystem::file_size(file_path);

    const std::string offer = "{\"type\":\"file_offer\",\"payload\":{\"name\":\"" +
                              transfer.name + "\",\"size\":" +
                              std::to_string(transfer.size) + "}}\n";
    if (!send_all(client_fd, offer.data(), offer.size())) {
      return std::unexpected("failed to send file offer to " + device_id);
    }

    std::ifstream input(file_path, std::ios::binary);
    if (!input.is_open()) {
      return std::unexpected("failed to open file: " + file_path.string());
    }

    std::array<char, 65536> chunk{};
    std::uint64_t total_sent = 0;
    while (input) {
      input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
      const auto got = static_cast<std::size_t>(input.gcount());
      if (got == 0) {
        break;
      }
      if (!send_all(client_fd, chunk.data(), got)) {
        return std::unexpected("failed while streaming file bytes to " + device_id);
      }
      total_sent += got;
    }

    transfer.completed = total_sent == transfer.size;
    if (callback_) {
      callback_(transfer);
    }
    return {};
  }

  return std::unexpected("device not found: " + device_id);
}

void MobileTransferServer::accept_loop() {
  while (running_) {
    const auto client_fd = accept(server_fd_, nullptr, nullptr);
    if (client_fd < 0) {
      if (running_) {
        std::cerr << "mobile: accept failed\n";
      }
      continue;
    }

    std::thread(&MobileTransferServer::handle_client, this, client_fd).detach();
  }
}

void MobileTransferServer::handle_client(const int client_fd) {
  char chunk[8192];
  std::string buffer;

  while (running_) {
    const auto received = platform::recv_retry(client_fd, chunk, sizeof(chunk));
    if (received <= 0) {
      break;
    }
    buffer.append(chunk, static_cast<std::size_t>(received));

    // Drain complete control lines, stopping as soon as a file offer is reached
    // so the binary payload that follows it is consumed by the offer handler.
    for (;;) {
      const auto newline = buffer.find('\n');
      if (newline == std::string::npos) {
        break;
      }
      const auto line = buffer.substr(0, newline);
      buffer.erase(0, newline + 1);

      if (line.find("\"type\":\"discover\"") != std::string::npos) {
        handle_discover(client_fd);
      } else if (line.find("\"type\":\"auth_request\"") != std::string::npos) {
        handle_auth(client_fd, line);
      } else if (line.find("\"type\":\"file_offer\"") != std::string::npos) {
        handle_file_offer(client_fd, line, buffer);
        break;
      } else if (line.find("\"type\":\"file_complete\"") != std::string::npos) {
        handle_file_complete(client_fd, line);
      } else if (line.find("\"type\":\"file_data\"") != std::string::npos) {
        // A stray data header without a preceding offer carries no filename.
        std::cerr << "mobile: ignoring file_data header without an offer\n";
      } else if (line.find("\"type\":\"ping\"") != std::string::npos) {
        handle_ping(client_fd);
      } else if (line.find("\"type\":\"pong\"") != std::string::npos) {
        // Reply to our own keepalive; nothing to answer.
      } else if (line.find("\"type\":\"capabilities\"") != std::string::npos) {
        handle_capabilities(client_fd, line);
      } else if (line.find("\"type\":\"clipboard_sync\"") != std::string::npos) {
        handle_clipboard_sync(client_fd, line);
      } else if (line.find("\"type\":\"screen_start\"") != std::string::npos ||
                 line.find("\"type\":\"screen_stop\"") != std::string::npos) {
        handle_screen_control(client_fd, line.find("screen_start") != std::string::npos
                                            ? "screen_start"
                                            : "screen_stop");
      } else if (line.find("\"type\":\"input_event\"") != std::string::npos) {
        handle_input_event(client_fd);
      }
    }
  }

  close(client_fd);
}

void MobileTransferServer::handle_discover(const int client_fd) {
  std::string response = "{\"type\":\"discover_response\",\"payload\":{\"services\":["
                         "{\"name\":\"hyprshare\",\"host\":\"" + json_escape(local_ipv4()) +
                         "\",\"port\":" +
                         std::to_string(port_) +
                         ",\"capabilities\":[\"file_transfer\",\"clipboard_sync\"]}"
                         "]}}\n";
  send_all(client_fd, response.data(), response.size());
}

void MobileTransferServer::handle_auth(const int client_fd, const std::string& payload) {
  MobileClient client;
  client.fd = client_fd;
  client.device_id = extract_quoted(payload, "device_id");
  client.device_name = extract_quoted(payload, "device_name");

  {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients_.push_back(client);
  }

  const std::string response = "{\"type\":\"auth_response\",\"payload\":{\"status\":\"ok\"}}\n";
  send_all(client_fd, response.data(), response.size());

  std::cout << "mobile: client connected: " << client.device_name << " (" << client.device_id << ")\n";
}

void MobileTransferServer::handle_file_offer(
  const int client_fd,
  const std::string& payload,
  std::string& buffer) {
  const auto name = extract_quoted(payload, "name");
  auto size = extract_size(payload);

  auto safe_name = std::filesystem::path(name).filename().string();
  if (safe_name.empty() || safe_name == "." || safe_name == "..") {
    safe_name = "received_file";
  }

  const std::string accept = "{\"type\":\"file_accept\",\"payload\":{}}\n";
  if (!send_all(client_fd, accept.data(), accept.size())) {
    return;
  }

  if (size == 0) {
    return;
  }

  // Bytes already sitting in `buffer` belong to the file payload.
  std::string payload_bytes = std::move(buffer);
  buffer.clear();

  std::array<char, 65536> chunk{};
  bool header_skipped = false;
  while (payload_bytes.size() < size) {
    // Read a full chunk, not just `remaining`: clients prepend a JSON header
    // line, and limiting the read to the declared file size can split that
    // header across reads so it can never be recognized.
    const auto n = platform::recv_retry(client_fd, chunk.data(), chunk.size());
    if (n <= 0) {
      break;
    }
    payload_bytes.append(chunk.data(), static_cast<std::size_t>(n));

    // Newer clients send a `{"type":"file_data",...}` control line before the
    // raw bytes. Strip it from the stream before treating bytes as file data.
    if (!header_skipped && payload_bytes.find("\"type\":\"file_data\"") != std::string::npos) {
      const auto newline = payload_bytes.find('\n');
      if (newline != std::string::npos) {
        const auto header = payload_bytes.substr(0, newline);
        const auto declared = extract_size(header);
        if (declared > 0) size = declared;
        payload_bytes.erase(0, newline + 1);
      }
      header_skipped = true;
    }
  }

  const auto destination = inbox_dir() / safe_name;
  std::ofstream output(destination, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    std::cerr << "mobile: failed to open " << destination.string() << " for writing\n";
    return;
  }
  output.write(payload_bytes.data(), static_cast<std::streamsize>(payload_bytes.size()));

  std::cout << "mobile: received " << safe_name << " (" << payload_bytes.size() << " bytes)\n";

  if (callback_) {
    FileTransfer transfer;
    transfer.name = safe_name;
    transfer.size = payload_bytes.size();
    transfer.remote_path = destination;
    transfer.completed = payload_bytes.size() == size;
    callback_(transfer);
  }
}

MobileClient* MobileTransferServer::client_for_fd(const int client_fd) {
  std::lock_guard<std::mutex> lock(clients_mutex_);
  for (auto& client : clients_) {
    if (client.fd == client_fd) {
      return &client;
    }
  }
  return nullptr;
}

void MobileTransferServer::handle_ping(const int client_fd) {
  const std::string response =
    "{\"type\":\"pong\",\"payload\":{\"timestamp\":" + now_millis() + "}}\n";
  send_all(client_fd, response.data(), response.size());
}

void MobileTransferServer::handle_capabilities(const int client_fd, const std::string& payload) {
  auto capabilities = extract_string_array(payload, "capabilities");

  std::string listing;
  for (const auto& capability : capabilities) {
    if (!listing.empty()) {
      listing += ",";
    }
    listing += "\"" + json_escape(capability) + "\"";
  }

  {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto& client : clients_) {
      if (client.fd == client_fd) {
        client.capabilities = capabilities;
        break;
      }
    }
  }

  const std::string response = "{\"type\":\"capabilities_ack\",\"payload\":{\"status\":\"ok\","
                               "\"count\":" + std::to_string(capabilities.size()) +
                               ",\"capabilities\":[" + listing + "]}}\n";
  send_all(client_fd, response.data(), response.size());
}

void MobileTransferServer::handle_clipboard_sync(
  const int client_fd,
  const std::string& payload) {
  const auto text = extract_quoted(payload, "text");

  std::string status = "ok";
  std::string reason;
  std::string backend = "wl-clipboard";

  platform::ProcessRunner runner;
  if (text.empty()) {
    status = "error";
    reason = "the clipboard_sync message carried no text";
  } else if (!runner.command_exists("wl-copy")) {
    status = "error";
    reason = "wl-copy is not installed on the host";
  } else {
    auto spawned = runner.spawn_with_stdin("wl-copy --type text/plain");
    if (!spawned.has_value()) {
      status = "error";
      reason = "failed to start wl-copy: " + spawned.error();
    } else {
      const auto written = runner.write_to_stdin(
        spawned->stdin_fd, text.data(), text.size());
      const auto closed = runner.close_stdin(spawned->stdin_fd);
      const auto exit_code = runner.wait_for_process(spawned->pid, 3000);
      if (!written.has_value()) {
        status = "error";
        reason = "failed to write to wl-copy: " + written.error();
      } else if (!closed.has_value()) {
        status = "error";
        reason = "failed to close wl-copy stdin: " + closed.error();
      } else if (!exit_code.has_value() || *exit_code != 0) {
        status = "error";
        reason = "wl-copy exited unsuccessfully (no compositor clipboard?)";
      }
    }
  }

  if (status == "ok") {
    std::cout << "mobile: clipboard received (" << text.size() << " bytes)\n";
  } else {
    std::cerr << "mobile: clipboard sync failed: " << reason << "\n";
  }

  std::string response = "{\"type\":\"clipboard_ack\",\"payload\":{\"status\":\"" + status + "\"";
  if (!reason.empty()) {
    response += ",\"reason\":\"" + json_escape(reason) + "\"";
  }
  if (status == "ok") {
    response += ",\"bytes\":" + std::to_string(text.size());
    response += ",\"backend\":\"" + json_escape(backend) + "\"";
  }
  response += "}}\n";
  send_all(client_fd, response.data(), response.size());
}

void MobileTransferServer::handle_file_complete(
  const int client_fd,
  const std::string& payload) {
  const auto name = std::filesystem::path(extract_quoted(payload, "name")).filename().string();
  const auto declared_size = extract_size(payload);
  const auto declared_sha256 = extract_quoted(payload, "sha256");

  std::string status = "ok";
  std::string reason;
  std::uintmax_t received_size = 0;
  std::string actual_sha256;
  bool sha256_match = true;

  const auto destination = inbox_dir() / name;
  std::error_code error;
  if (name.empty() || !std::filesystem::exists(destination, error)) {
    status = "missing";
    reason = "no file with that name was received";
  } else {
    received_size = std::filesystem::file_size(destination, error);
    if (declared_size != 0 && received_size != declared_size) {
      status = "mismatch";
      reason = "received size differs from the announced size";
    } else if (!declared_sha256.empty()) {
      const auto computed = file_sha256(destination);
      if (!computed.has_value()) {
        status = "error";
        reason = computed.error();
      } else {
        actual_sha256 = *computed;
        sha256_match = *computed == declared_sha256;
        if (!sha256_match) {
          status = "mismatch";
          reason = "received checksum differs from the announced checksum";
        }
      }
    }
  }

  std::cout << "mobile: file_complete " << name << " -> " << status << " ("
            << received_size << " bytes)\n";

  std::string response = "{\"type\":\"file_complete_ack\",\"payload\":{\"status\":\"" + status +
                         "\",\"name\":\"" + json_escape(name) + "\""
                         ",\"received_size\":" + std::to_string(received_size) +
                         ",\"sha256_match\":" + (sha256_match ? "true" : "false");
  if (!actual_sha256.empty()) {
    response += ",\"sha256\":\"" + actual_sha256 + "\"";
  }
  if (!reason.empty()) {
    response += ",\"reason\":\"" + json_escape(reason) + "\"";
  }
  response += "}}\n";
  send_all(client_fd, response.data(), response.size());
}

void MobileTransferServer::handle_screen_control(
  const int client_fd,
  const std::string& type) {
  // Streaming frames to the phone is not wired up: the recorder writes to a
  // local file through xdg-desktop-portal, and there is no frame path into this
  // socket yet. Answer honestly instead of accepting and sending nothing.
  const std::string response =
    "{\"type\":\"screen_status\",\"payload\":{\"status\":\"unsupported\""
    ",\"request\":\"" + json_escape(type) + "\""
    ",\"reason\":\"mobile screen streaming is not implemented on the host; "
    "use `hyprshare record` for a local capture\"}}\n";
  send_all(client_fd, response.data(), response.size());
  std::cerr << "mobile: " << type << " declined (screen streaming unsupported)\n";
}

void MobileTransferServer::handle_input_event(const int client_fd) {
  // Injected input would need ydotool/wtype under Wayland; neither is a hard
  // dependency of this package, so report the limitation rather than dropping
  // the event silently.
  const std::string response =
    "{\"type\":\"input_ack\",\"payload\":{\"status\":\"unsupported\""
    ",\"reason\":\"remote input injection requires ydotool or wtype, neither of "
    "which is available on the host\"}}\n";
  send_all(client_fd, response.data(), response.size());
  std::cerr << "mobile: input_event declined (no injection backend)\n";
}

}  // namespace hyprshare::mobile
