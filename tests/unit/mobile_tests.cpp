#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "hyprshare/mobile/mobile_transfer.hpp"

#include "test_support.hpp"

namespace {

using test_support::expect;

std::uint16_t find_free_port() {
  const int probe = socket(AF_INET, SOCK_STREAM, 0);
  if (probe < 0) {
    return 0;
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (bind(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    close(probe);
    return 0;
  }
  socklen_t length = sizeof(address);
  if (getsockname(probe, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
    close(probe);
    return 0;
  }
  const auto port = ntohs(address.sin_port);
  close(probe);
  return port;
}

class TestClient {
 public:
  explicit TestClient(const std::uint16_t port) {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (connect(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
      close(fd_);
      fd_ = -1;
    }
  }

  ~TestClient() {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

  TestClient(const TestClient&) = delete;
  TestClient& operator=(const TestClient&) = delete;

  bool valid() const { return fd_ >= 0; }

  void send_raw(const std::string& bytes) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
      const auto n = send(fd_, bytes.data() + sent, bytes.size() - sent, 0);
      if (n <= 0) {
        return;
      }
      sent += static_cast<std::size_t>(n);
    }
  }

  void send_message(const std::string& type, const std::string& payload) {
    send_raw("{\"type\":\"" + type + "\",\"payload\":" + payload + "}\n");
  }

  // Reads one newline-terminated control message.
  std::string read_line() {
    std::string line;
    std::array<char, 4096> chunk{};
    for (;;) {
      const auto newline = buffer_.find('\n');
      if (newline != std::string::npos) {
        line = buffer_.substr(0, newline);
        buffer_.erase(0, newline + 1);
        return line;
      }
      const auto n = recv(fd_, chunk.data(), chunk.size(), 0);
      if (n <= 0) {
        return "";
      }
      buffer_.append(chunk.data(), static_cast<std::size_t>(n));
    }
  }

 private:
  int fd_ = -1;
  std::string buffer_;
};

std::uintmax_t file_size_of(const std::filesystem::path& path) {
  std::error_code error;
  return std::filesystem::file_size(path, error);
}

}  // namespace

int main() {
  const auto sandbox = std::filesystem::temp_directory_path() / "hyprshare_mobile_tests";
  std::error_code error;
  std::filesystem::remove_all(sandbox, error);
  std::filesystem::create_directories(sandbox, error);
  setenv("HOME", sandbox.c_str(), 1);

  const auto port = find_free_port();
  expect(port != 0, "a free local port should be available for the server");

  hyprshare::mobile::MobileTransferServer server(port);
  const auto started = server.start();
  expect(started.has_value(), "the mobile server should start: " +
                                (started.has_value() ? std::string() : started.error()));

  TestClient client(port);
  expect(client.valid(), "the test client should connect to the server");

  // discover must advertise a reachable address instead of a wildcard bind.
  client.send_message("discover", "{\"protocol_version\":\"1.0\"}");
  const auto discover = client.read_line();
  expect(discover.find("\"host\":\"0.0.0.0\"") == std::string::npos,
         "discover must not advertise the wildcard address");
  expect(discover.find("\"port\":" + std::to_string(port)) != std::string::npos,
         "discover should report the listening port");

  client.send_message("auth_request", "{\"device_id\":\"d1\",\"device_name\":\"tests\"}");
  expect(client.read_line().find("\"status\":\"ok\"") != std::string::npos,
         "auth_request should be accepted");

  // ping/pong keepalive.
  client.send_message("ping", "{}");
  const auto pong = client.read_line();
  expect(pong.find("\"type\":\"pong\"") != std::string::npos,
         "ping should be answered with pong");
  expect(pong.find("\"timestamp\":") != std::string::npos,
         "pong should carry a real timestamp");

  // capabilities are acknowledged and echoed back.
  client.send_message("capabilities",
                      "{\"capabilities\":[\"file_transfer\",\"clipboard_sync\"]}");
  const auto capabilities = client.read_line();
  expect(capabilities.find("\"type\":\"capabilities_ack\"") != std::string::npos,
         "capabilities should be acknowledged");
  expect(capabilities.find("\"count\":2") != std::string::npos,
         "capabilities_ack should count the reported capabilities");

  // A client-supplied capabilities message must not crash on an empty array.
  client.send_message("capabilities", "{\"capabilities\":[]}");
  expect(client.read_line().find("\"count\":0") != std::string::npos,
         "an empty capabilities array should be acknowledged with count 0");

  // File transfer followed by an explicit completion check.
  const std::string content = "mobile protocol test payload";
  client.send_message("file_offer", "{\"name\":\"sample.txt\",\"size\":" +
                                      std::to_string(content.size()) + "}");
  expect(client.read_line().find("\"type\":\"file_accept\"") != std::string::npos,
         "file_offer should be accepted");
  client.send_message("file_data", "{\"file_id\":\"f1\",\"offset\":0,\"size\":" +
                                     std::to_string(content.size()) + "}");
  client.send_raw(content);

  const auto received = sandbox / ".local/share/hyprshare/inbox" / "sample.txt";
  for (int attempt = 0; attempt < 100; ++attempt) {
    if (file_size_of(received) == content.size()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  expect(file_size_of(received) == content.size(),
         "the served file should hold exactly the transferred bytes");

  std::ifstream input(received, std::ios::binary);
  std::ostringstream read_back;
  read_back << input.rdbuf();
  expect(read_back.str() == content,
         "the file_data control header must not leak into the received file");

  client.send_message("file_complete", "{\"name\":\"sample.txt\",\"size\":" +
                                         std::to_string(content.size()) + "}");
  const auto complete = client.read_line();
  expect(complete.find("\"status\":\"ok\"") != std::string::npos,
         "file_complete should confirm a correctly received file");

  // A wrong announced size must be reported as a mismatch, not accepted.
  client.send_message("file_complete",
                      "{\"name\":\"sample.txt\",\"size\":999999}");
  expect(client.read_line().find("\"status\":\"mismatch\"") != std::string::npos,
         "file_complete should reject a size that does not match the file");

  client.send_message("file_complete", "{\"name\":\"absent.bin\",\"size\":10}");
  expect(client.read_line().find("\"status\":\"missing\"") != std::string::npos,
         "file_complete should report a file that was never received");

  // Unimplemented remote features must answer honestly instead of pretending.
  client.send_message("screen_start", "{\"quality\":80,\"fps\":30}");
  const auto screen = client.read_line();
  expect(screen.find("\"type\":\"screen_status\"") != std::string::npos,
         "screen_start should be answered");
  expect(screen.find("\"status\":\"unsupported\"") != std::string::npos,
         "screen_start should report unsupported rather than a fake success");

  client.send_message("input_event", "{\"action\":\"click\",\"x\":1,\"y\":2}");
  expect(client.read_line().find("\"status\":\"unsupported\"") != std::string::npos,
         "input_event should report unsupported rather than a fake success");

  // The connection must stay usable after every handled message type.
  client.send_message("ping", "{}");
  expect(client.read_line().find("\"type\":\"pong\"") != std::string::npos,
         "the connection should stay usable after all message types");

  server.stop();
  std::filesystem::remove_all(sandbox, error);
  return test_support::report("hyprshare_mobile_tests");
}
