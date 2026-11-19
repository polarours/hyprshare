#pragma once

#include <atomic>
#include <thread>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace hyprshare::mobile {

struct MobileClient {
  std::string device_id;
  std::string device_name;
  std::string address;
  int fd = -1;
};

struct FileTransfer {
  std::string file_id;
  std::string name;
  uint64_t size = 0;
  std::string mime_type;
  std::string sha256;
  std::filesystem::path local_path;
  std::filesystem::path remote_path;
  bool completed = false;
};

using TransferCallback = std::function<void(const FileTransfer&)>;

class MobileTransferServer {
 public:
  MobileTransferServer(uint16_t port = 28556);
  ~MobileTransferServer();

  std::expected<void, std::string> start();
  void stop();

  std::expected<void, std::string> send_file(
    const std::string& device_id,
    const std::filesystem::path& file_path);

  void set_transfer_callback(TransferCallback callback);

  bool is_running() const;

 private:
  void accept_loop();
  void handle_client(int client_fd);
  void handle_discover(int client_fd);
  void handle_auth(int client_fd, const std::string& payload);
  void handle_file_offer(int client_fd, const std::string& payload, std::string& buffer);

  uint16_t port_;
  int server_fd_ = -1;
  std::thread accept_thread_;
  std::atomic<bool> running_{false};
  TransferCallback callback_;
  std::vector<MobileClient> clients_;
};

}  // namespace hyprshare::mobile
