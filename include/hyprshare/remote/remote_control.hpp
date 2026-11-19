#pragma once

#include <atomic>
#include <thread>
#include <cstdint>
#include <expected>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace hyprshare::remote {

enum class InputEventType {
  Touch,
  Keyboard,
  Gesture,
};

struct InputEvent {
  InputEventType type = InputEventType::Touch;
  int32_t x = 0;
  int32_t y = 0;
  std::string action;
  std::string key;
};

struct ScreenFrame {
  uint32_t width = 0;
  uint32_t height = 0;
  std::string format;
  std::vector<uint8_t> data;
};

using InputCallback = std::function<void(const InputEvent&)>;
using FrameCallback = std::function<void(const ScreenFrame&)>;

class RemoteControlServer {
 public:
  RemoteControlServer(uint16_t port = 28557);
  ~RemoteControlServer();

  std::expected<void, std::string> start();
  void stop();

  std::expected<void, std::string> send_screen_frame(
    uint32_t width,
    uint32_t height,
    const uint8_t* data,
    size_t size);

  void set_input_callback(InputCallback callback);
  void set_frame_callback(FrameCallback callback);

  bool is_running() const;

 private:
  void accept_loop();
  void handle_client(int client_fd);

  uint16_t port_;
  int server_fd_ = -1;
  std::thread accept_thread_;
  std::atomic<bool> running_{false};
  InputCallback input_callback_;
  FrameCallback frame_callback_;
  std::vector<int> client_fds_;
  mutable std::mutex client_mutex_;
};

}  // namespace hyprshare::remote
