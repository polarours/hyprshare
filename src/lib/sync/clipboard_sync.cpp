#include "hyprshare/sync/clipboard_sync.hpp"

#include <algorithm>
#include <chrono>
#include <string_view>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#include "hyprshare/core/clipboard_share.hpp"
#include "hyprshare/core/inbox.hpp"

namespace hyprshare::sync {

namespace {

// How often the system clipboard is polled for changes.
constexpr std::chrono::milliseconds kPollInterval{1000};

std::filesystem::path history_root(const platform::Paths& paths) {
  return paths.state_dir / "clipboard-history";
}

std::int64_t unix_time_now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
           std::chrono::system_clock::now().time_since_epoch())
    .count();
}

std::string make_entry_id() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

std::expected<void, std::string> write_history_file(
  const ClipboardEntry& entry, const std::filesystem::path& path) {
  std::ofstream output(path);
  if (!output.is_open()) {
    return std::unexpected("failed to open clipboard history file for writing: " + path.string());
  }
  output << "entry_id=" << entry.entry_id << "\n";
  output << "mime_type=" << entry.mime_type << "\n";
  output << "timestamp=" << entry.timestamp << "\n";
  output << "source_device_id=" << entry.source_device_id << "\n";
  return {};
}

std::optional<ClipboardEntry> load_history_file(
  const std::filesystem::path& path, const std::filesystem::path& content_path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    return std::nullopt;
  }

  ClipboardEntry entry;
  std::string line;
  while (std::getline(input, line)) {
    const std::size_t separator = line.find('=');
    if (separator == std::string::npos) {
      continue;
    }
    const std::string key = line.substr(0, separator);
    const std::string value = line.substr(separator + 1);
    if (key == "entry_id") {
      entry.entry_id = value;
    } else if (key == "mime_type") {
      entry.mime_type = value;
    } else if (key == "timestamp") {
      try {
        entry.timestamp = std::stoll(value);
      } catch (const std::exception&) {
        return std::nullopt;
      }
    } else if (key == "source_device_id") {
      entry.source_device_id = value;
    }
  }

  std::ifstream content(content_path, std::ios::binary);
  if (!content.is_open()) {
    return std::nullopt;
  }
  std::ostringstream buffer;
  buffer << content.rdbuf();
  entry.content = buffer.str();
  return entry;
}

}  // namespace

ClipboardSyncService::ClipboardSyncService()
  : paths_(platform::resolve_paths()), runner_() {}

ClipboardSyncService::~ClipboardSyncService() {
  stop();
}

std::expected<void, std::string> ClipboardSyncService::start() {
  if (running_) {
    return std::unexpected("clipboard sync is already running");
  }

  // Seed the baseline so that the content that is already on the clipboard
  // does not immediately look like a change.
  const auto current = core::read_clipboard_text(paths_, runner_);
  if (current.has_value()) {
    last_content_ = current->text;
  }

  running_ = true;
  std::thread(&ClipboardSyncService::watch_loop, this).detach();
  return {};
}

void ClipboardSyncService::stop() {
  running_ = false;
}

std::expected<void, std::string> ClipboardSyncService::push(
  const std::string& content,
  const std::string& mime_type) {
  ClipboardEntry entry;
  entry.entry_id = make_entry_id();
  entry.content = content;
  entry.mime_type = mime_type;
  entry.timestamp = unix_time_now();

  const auto stored = store(entry);
  if (!stored.has_value()) {
    return stored;
  }

  history_.push_back(entry);

  // The pushed content becomes the baseline, so that the watch loop does not
  // report it again as an external change.
  last_content_ = content;

  if (callback_) {
    callback_(entry);
  }

  return {};
}

std::expected<ClipboardEntry, std::string> ClipboardSyncService::pull() {
  const auto entries = load_history();
  if (entries.empty()) {
    return std::unexpected("clipboard history is empty");
  }
  return entries.back();
}

std::vector<ClipboardEntry> ClipboardSyncService::history() const {
  return load_history();
}

std::expected<void, std::string> ClipboardSyncService::push_to_peer(
  const std::string& host,
  const std::uint16_t port,
  const std::string& device_id,
  const std::string& access_token) {
  const auto entries = load_history();
  if (entries.empty()) {
    return std::unexpected("there is nothing to send yet — run 'clipboard-sync push <content>' first");
  }

  const auto& entry = entries.back();
  const auto scratch = paths_.state_dir / "clipboard-outbox";
  std::error_code error;
  std::filesystem::create_directories(scratch, error);
  if (error) {
    return std::unexpected("failed to create the outbox directory: " + error.message());
  }

  const auto remote_name = (device_id.empty() ? std::string("device") : device_id) + ".clipboard.txt";
  const auto file = scratch / remote_name;
  {
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
      return std::unexpected("failed to write the outbox file: " + file.string());
    }
    output << entry.content;
  }

  const auto sent = core::put_inbox_file(host, port, file, remote_name, access_token);
  if (!sent.has_value()) {
    return std::unexpected("failed to send the clipboard: " + sent.error());
  }
  return {};
}

std::expected<ClipboardEntry, std::string> ClipboardSyncService::pull_from_peer(
  const std::string& host,
  const std::uint16_t port,
  const std::string& access_token) {
  const auto listing = core::list_inbox_files(host, port, access_token);
  if (!listing.has_value()) {
    return std::unexpected("failed to list the files of the peer: " + listing.error());
  }

  const auto scratch = paths_.state_dir / "clipboard-inbox";
  std::error_code error;
  std::filesystem::create_directories(scratch, error);

  // The clipboard files are named after the device that sent them.
  static constexpr std::string_view kClipboardSuffix = ".clipboard.txt";

  for (const auto& remote : *listing) {
    if (remote.name.size() < kClipboardSuffix.size() ||
        remote.name.compare(remote.name.size() - kClipboardSuffix.size(),
          kClipboardSuffix.size(), kClipboardSuffix) != 0) {
      continue;
    }

    const auto file = scratch / remote.name;
    const auto downloaded = core::get_inbox_file(host, port, remote.name, file, access_token);
    if (!downloaded.has_value()) {
      return std::unexpected("failed to fetch the clipboard: " + downloaded.error());
    }

    std::ifstream input(file, std::ios::binary);
    if (!input.is_open()) {
      return std::unexpected("failed to read the clipboard file: " + file.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();

    ClipboardEntry entry;
    entry.content = buffer.str();
    entry.mime_type = "text/plain";
    entry.timestamp = remote.modified_at_unix;
    entry.source_device_id = remote.name.substr(0, remote.name.size() - kClipboardSuffix.size());
    history_.push_back(entry);
    return entry;
  }

  return std::unexpected("the peer has not sent a clipboard yet");
}

void ClipboardSyncService::set_clipboard_callback(ClipboardCallback callback) {
  callback_ = std::move(callback);
}

bool ClipboardSyncService::is_running() const {
  return running_;
}

std::expected<void, std::string> ClipboardSyncService::store(const ClipboardEntry& entry) {
  const auto root = history_root(paths_);
  std::error_code error;
  std::filesystem::create_directories(root, error);
  if (error) {
    return std::unexpected("failed to create clipboard history directory: " + error.message());
  }

  const auto meta_path = root / entry.entry_id;
  const auto content_path = root / (entry.entry_id + ".content");

  {
    std::ofstream content(content_path, std::ios::binary);
    if (!content.is_open()) {
      return std::unexpected("failed to open clipboard content file: " + content_path.string());
    }
    content << entry.content;
  }

  auto written = write_history_file(entry, meta_path);
  if (!written.has_value()) {
    return written;
  }
  return {};
}

std::vector<ClipboardEntry> ClipboardSyncService::load_history() const {
  std::vector<ClipboardEntry> entries;
  const auto root = history_root(paths_);
  std::error_code error;
  if (!std::filesystem::exists(root, error)) {
    return entries;
  }

  for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
    if (!entry.is_regular_file() || entry.path().extension() == ".content") {
      continue;
    }
    const auto loaded = load_history_file(
      entry.path(), entry.path().string() + ".content");
    if (loaded.has_value()) {
      entries.push_back(*loaded);
    }
  }

  std::sort(entries.begin(), entries.end(),
    [](const ClipboardEntry& a, const ClipboardEntry& b) { return a.timestamp < b.timestamp; });
  return entries;
}

void ClipboardSyncService::watch_loop() {
  while (running_) {
    const auto clipboard = core::read_clipboard_text(paths_, runner_);
    if (clipboard.has_value() && !clipboard->text.empty() && clipboard->text != last_content_) {
      last_content_ = clipboard->text;

      ClipboardEntry entry;
      entry.entry_id = make_entry_id();
      entry.content = clipboard->text;
      entry.mime_type = "text/plain";
      entry.timestamp = clipboard->captured_at_unix;

      const auto stored = store(entry);
      if (stored.has_value()) {
        history_.push_back(entry);
        if (callback_) {
          callback_(entry);
        }
      }
    }

    std::this_thread::sleep_for(kPollInterval);
  }
}

}  // namespace hyprshare::sync
