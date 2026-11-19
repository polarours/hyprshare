#include "hyprshare/core/inbox_history.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <sstream>

namespace hyprshare::core {

namespace {

std::filesystem::path inbox_history_root(const platform::Paths& paths) { return paths.state_dir / "inbox-history"; }

std::int64_t unix_time_now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
           std::chrono::system_clock::now().time_since_epoch())
    .count();
}

std::string make_history_entry_id() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

std::expected<void, std::string> ensure_directory(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::create_directories(path, error);
  if (error) {
    return std::unexpected("failed to create directory `" + path.string() + "`: " + error.message());
  }
  return {};
}

std::expected<std::map<std::string, std::string>, std::string> read_metadata_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    return std::unexpected("failed to open inbox history file: " + path.string());
  }

  std::map<std::string, std::string> values;
  std::string line;
  while (std::getline(input, line)) {
    const auto separator = line.find('=');
    if (separator == std::string::npos) {
      continue;
    }
    values.emplace(line.substr(0, separator), line.substr(separator + 1));
  }
  return values;
}

std::expected<void, std::string> write_history_file(
  const model::InboxHistoryEntry& entry,
  const std::filesystem::path& path) {
  std::ofstream output(path);
  if (!output.is_open()) {
    return std::unexpected("failed to open inbox history file for writing: " + path.string());
  }

  output << "entry_id=" << entry.entry_id << "\n";
  output << "file_name=" << entry.file_name << "\n";
  output << "destination_path=" << entry.destination_path << "\n";
  output << "bytes_received=" << entry.bytes_received << "\n";
  if (!entry.sha256.empty()) {
    output << "sha256=" << entry.sha256 << "\n";
  }
  output << "received_at_unix=" << entry.received_at_unix << "\n";
  return {};
}

std::expected<model::InboxHistoryEntry, std::string> load_history_file(const std::filesystem::path& path) {
  const auto values = read_metadata_file(path);
  if (!values.has_value()) {
    return std::unexpected(values.error());
  }

  const auto entry_id_it = values->find("entry_id");
  const auto file_name_it = values->find("file_name");
  const auto destination_path_it = values->find("destination_path");
  const auto bytes_received_it = values->find("bytes_received");
  const auto sha256_it = values->find("sha256");
  const auto received_at_it = values->find("received_at_unix");
  if (entry_id_it == values->end() || file_name_it == values->end() || destination_path_it == values->end() ||
      bytes_received_it == values->end() || received_at_it == values->end()) {
    return std::unexpected("inbox history file is missing required fields: " + path.string());
  }

  model::InboxHistoryEntry entry;
  try {
    entry.entry_id = entry_id_it->second;
    entry.file_name = file_name_it->second;
    entry.destination_path = destination_path_it->second;
    entry.bytes_received = static_cast<std::uintmax_t>(std::stoull(bytes_received_it->second));
    if (sha256_it != values->end()) {
      entry.sha256 = sha256_it->second;
    }
    entry.received_at_unix = std::stoll(received_at_it->second);
  } catch (const std::exception&) {
    return std::unexpected("inbox history file contains an invalid numeric field: " + path.string());
  }
  return entry;
}

}  // namespace

std::expected<model::InboxHistoryEntry, std::string> append_inbox_history(
  const model::InboxReceivedFile& received,
  const platform::Paths& paths) {
  const auto history_root = inbox_history_root(paths);
  const auto ensure_result = ensure_directory(history_root);
  if (!ensure_result.has_value()) {
    return std::unexpected(ensure_result.error());
  }

  model::InboxHistoryEntry entry;
  entry.entry_id = make_history_entry_id();
  entry.file_name = received.file_name;
  entry.destination_path = received.destination_path;
  entry.bytes_received = received.bytes_received;
  entry.sha256 = received.sha256;
  entry.received_at_unix = unix_time_now();

  auto history_path = history_root / (entry.entry_id + ".meta");
  for (int suffix = 1; std::filesystem::exists(history_path); ++suffix) {
    entry.entry_id = make_history_entry_id() + "-" + std::to_string(suffix);
    history_path = history_root / (entry.entry_id + ".meta");
  }
  const auto write_result = write_history_file(entry, history_path);
  if (!write_result.has_value()) {
    return std::unexpected(write_result.error());
  }
  return entry;
}

std::expected<std::vector<model::InboxHistoryEntry>, std::string> list_inbox_history(
  const platform::Paths& paths) {
  const auto history_root = inbox_history_root(paths);
  if (!std::filesystem::exists(history_root)) {
    return std::vector<model::InboxHistoryEntry>{};
  }

  std::vector<model::InboxHistoryEntry> entries;
  for (const auto& entry : std::filesystem::directory_iterator(history_root)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto loaded = load_history_file(entry.path());
    if (!loaded.has_value()) {
      return std::unexpected(loaded.error());
    }
    entries.push_back(*loaded);
  }

  std::sort(entries.begin(), entries.end(), [](const model::InboxHistoryEntry& left, const model::InboxHistoryEntry& right) {
    if (left.received_at_unix != right.received_at_unix) {
      return left.received_at_unix > right.received_at_unix;
    }
    return left.entry_id > right.entry_id;
  });
  return entries;
}

std::expected<std::size_t, std::string> clear_inbox_history(
  const platform::Paths& paths) {
  const auto history_root = inbox_history_root(paths);
  if (!std::filesystem::exists(history_root)) {
    return static_cast<std::size_t>(0);
  }

  std::size_t removed = 0;
  for (const auto& entry : std::filesystem::directory_iterator(history_root)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    std::error_code error;
    const bool deleted = std::filesystem::remove(entry.path(), error);
    if (error) {
      return std::unexpected("failed to remove inbox history file `" + entry.path().string() + "`: " + error.message());
    }
    if (deleted) {
      ++removed;
    }
  }
  return removed;
}

}  // namespace hyprshare::core
