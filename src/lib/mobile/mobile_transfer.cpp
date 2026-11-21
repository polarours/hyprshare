#include "hyprshare/platform/net.hpp"
#include "hyprshare/mobile/mobile_transfer.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
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
      }
    }
  }

  close(client_fd);
}

void MobileTransferServer::handle_discover(const int client_fd) {
  std::string response = "{\"type\":\"discover_response\",\"payload\":{\"services\":["
                         "{\"name\":\"hyprshare\",\"host\":\"0.0.0.0\",\"port\":" +
                         std::to_string(port_) +
                         ",\"capabilities\":[\"file_transfer\",\"screen_share\",\"remote_control\"]}"
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
  const auto size = extract_size(payload);

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
  while (payload_bytes.size() < size) {
    const auto remaining = size - payload_bytes.size();
    const auto to_copy = std::min(remaining, chunk.size());
    const auto n = platform::recv_retry(client_fd, chunk.data(), to_copy);
    if (n <= 0) {
      break;
    }
    payload_bytes.append(chunk.data(), static_cast<std::size_t>(n));
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

}  // namespace hyprshare::mobile
