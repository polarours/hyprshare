#pragma once

#include <atomic>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "hyprshare/platform/paths.hpp"
#include "hyprshare/platform/process.hpp"

namespace hyprshare::sync {

struct ClipboardEntry {
  std::string entry_id;
  std::string content;
  std::string mime_type;
  int64_t timestamp = 0;
  std::string source_device_id;
};

using ClipboardCallback = std::function<void(const ClipboardEntry&)>;

class ClipboardSyncService {
 public:
  ClipboardSyncService();
  ~ClipboardSyncService();

  std::expected<void, std::string> start();
  void stop();

  std::expected<void, std::string> push(const std::string& content, const std::string& mime_type = "text/plain");
  std::expected<ClipboardEntry, std::string> pull();
  std::vector<ClipboardEntry> history() const;

  // Sends the current clipboard content to a peer. The content travels as a
  // file in the peer's inbox, named after the sending device.
  std::expected<void, std::string> push_to_peer(
    const std::string& host,
    std::uint16_t port,
    const std::string& device_id,
    const std::string& access_token = {});

  // Fetches the clipboard content that a peer sent.
  std::expected<ClipboardEntry, std::string> pull_from_peer(
    const std::string& host,
    std::uint16_t port,
    const std::string& access_token = {});

  void set_clipboard_callback(ClipboardCallback callback);

  bool is_running() const;

 private:
  void watch_loop();

  // Persists an entry in the clipboard history directory.
  std::expected<void, std::string> store(const ClipboardEntry& entry);
  // Reads the persisted history, oldest entry first.
  std::vector<ClipboardEntry> load_history() const;

  std::atomic<bool> running_{false};
  std::vector<ClipboardEntry> history_;
  ClipboardCallback callback_;
  std::string last_content_;
  platform::Paths paths_;
  platform::ProcessRunner runner_;
};

}  // namespace hyprshare::sync
