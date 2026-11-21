#include "hyprshare/sync/file_sync.hpp"

#include <chrono>
#include <thread>

#include "hyprshare/core/inbox.hpp"

namespace hyprshare::sync {

namespace {

// Splits an address of the form host:port.
std::expected<std::pair<std::string, std::uint16_t>, std::string> parse_address(
  const std::string& address) {
  const auto separator = address.rfind(':');
  if (separator == std::string::npos) {
    return std::unexpected("the peer address needs a port: " + address);
  }

  const std::string host = address.substr(0, separator);
  const std::string port_text = address.substr(separator + 1);
  if (host.empty()) {
    return std::unexpected("the peer address needs a host: " + address);
  }

  try {
    const long port = std::stol(port_text);
    if (port <= 0 || port > 65535) {
      return std::unexpected("the peer address has an invalid port: " + port_text);
    }
    return std::make_pair(host, static_cast<std::uint16_t>(port));
  } catch (const std::exception&) {
    return std::unexpected("the peer address has an invalid port: " + port_text);
  }
}

}  // namespace

FileSyncService::FileSyncService() {}

FileSyncService::~FileSyncService() {
  stop();
}

std::expected<void, std::string> FileSyncService::start() {
  if (running_) {
    return std::unexpected("file sync is already running");
  }
  running_ = true;
  status_ = SyncStatus::Idle;
  std::thread(&FileSyncService::watch_loop, this).detach();
  return {};
}

void FileSyncService::stop() {
  running_ = false;
  status_ = SyncStatus::Idle;
}

std::expected<void, std::string> FileSyncService::add_rule(const SyncRule& rule) {
  if (rule.id.empty()) {
    return std::unexpected("a sync rule needs an id");
  }
  if (rule.local_path.empty()) {
    return std::unexpected("a sync rule needs a local path");
  }
  for (const auto& existing : rules_) {
    if (existing.id == rule.id) {
      return std::unexpected("a sync rule with this id already exists: " + rule.id);
    }
  }
  rules_.push_back(rule);
  return {};
}

std::expected<void, std::string> FileSyncService::remove_rule(const std::string& rule_id) {
  const auto before = rules_.size();
  rules_.erase(
    std::remove_if(rules_.begin(), rules_.end(),
      [&rule_id](const SyncRule& r) { return r.id == rule_id; }),
    rules_.end());
  if (rules_.size() == before) {
    return std::unexpected("rule not found: " + rule_id);
  }
  return {};
}

std::vector<SyncRule> FileSyncService::list_rules() const {
  return rules_;
}

std::expected<SyncReport, std::string> FileSyncService::sync_now(const std::string& rule_id) {
  for (const auto& rule : rules_) {
    if (rule.id == rule_id) {
      return sync_rule(rule);
    }
  }
  return std::unexpected("rule not found: " + rule_id);
}

std::expected<void, std::string> FileSyncService::pause(const std::string& rule_id) {
  bool found = false;
  for (const auto& rule : rules_) {
    if (rule.id == rule_id) {
      found = true;
      break;
    }
  }
  if (!found) {
    return std::unexpected("rule not found: " + rule_id);
  }
  paused_rules_.insert(rule_id);
  status_ = SyncStatus::Paused;
  return {};
}

std::expected<void, std::string> FileSyncService::resume(const std::string& rule_id) {
  if (paused_rules_.erase(rule_id) == 0) {
    return std::unexpected("rule was not paused: " + rule_id);
  }
  if (paused_rules_.empty()) {
    status_ = SyncStatus::Idle;
  }
  return {};
}

bool FileSyncService::is_paused(const std::string& rule_id) const {
  return paused_rules_.count(rule_id) > 0;
}

void FileSyncService::set_sync_callback(SyncCallback callback) {
  callback_ = std::move(callback);
}

void FileSyncService::set_tls_config(const tls::TlsTransportConfig& config) {
  tls_config_ = config;
}

SyncStatus FileSyncService::status() const {
  return status_;
}

std::vector<SyncEvent> FileSyncService::recent_events() const {
  return events_;
}

bool FileSyncService::is_running() const {
  return running_;
}

std::vector<std::filesystem::path> FileSyncService::collect_files(const SyncRule& rule) const {
  std::vector<std::filesystem::path> files;
  std::error_code error;

  if (std::filesystem::is_directory(rule.local_path, error)) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(rule.local_path, error)) {
      if (entry.is_regular_file()) {
        files.push_back(entry.path());
      }
    }
  } else if (std::filesystem::exists(rule.local_path, error)) {
    files.push_back(rule.local_path);
  }

  return files;
}

SyncReport FileSyncService::sync_rule(const SyncRule& rule) {
  SyncReport report;

  const auto address = parse_address(rule.remote_address);
  if (!address.has_value()) {
    report.failures.push_back(address.error());
    return report;
  }

  const bool pull = rule.direction == SyncDirection::RemoteToLocal ||
                    rule.direction == SyncDirection::Bidirectional;
  const bool push = rule.direction == SyncDirection::LocalToRemote ||
                    rule.direction == SyncDirection::Bidirectional;

  if (pull) {
    // The peer inbox lists what it holds. Files that are already there with
    // the same size are left alone.
    const auto listing = core::list_inbox_files(
      address->first, address->second, rule.remote_token, tls_config_);
    if (!listing.has_value()) {
      report.failures.push_back("failed to list the files of the peer: " + listing.error());
    } else {
      for (const auto& remote : *listing) {
        const auto target = rule.local_path / remote.name;
        std::error_code file_error;
        if (std::filesystem::exists(target, file_error) &&
            std::filesystem::file_size(target, file_error) == remote.bytes) {
          continue;
        }

        const auto downloaded = core::get_inbox_file(
          address->first, address->second, remote.name, target, rule.remote_token, tls_config_);
        if (!downloaded.has_value()) {
          report.failures.push_back(remote.name + ": " + downloaded.error());
          continue;
        }

        report.files_received += 1;
        report.bytes_received += *downloaded;

        SyncEvent event;
        event.rule_id = rule.id;
        event.path = target;
        event.event_type = "received";
        event.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch()).count();
        events_.push_back(event);
        if (callback_) {
          callback_(event);
        }
      }
    }
  }

  if (!push) {
    return report;
  }

  const auto files = collect_files(rule);
  if (files.empty()) {
    report.failures.push_back("no files were found in " + rule.local_path.string());
    return report;
  }

  for (const auto& file : files) {
    std::error_code error;
    auto relative = std::filesystem::relative(file, rule.local_path, error);
    const std::string remote_name = error ? file.filename().string() : relative.string();

    const auto sent = core::put_inbox_file(
      address->first,
      address->second,
      file,
      remote_name,
      rule.remote_token,
      &runner_,
      tls_config_);

    if (!sent.has_value()) {
      report.failures.push_back(remote_name + ": " + sent.error());
      continue;
    }

    report.files_sent += 1;
    report.bytes_sent += *sent;

    SyncEvent event;
    event.rule_id = rule.id;
    event.path = file;
    event.event_type = "sent";
    event.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
    events_.push_back(event);
    if (callback_) {
      callback_(event);
    }
  }

  return report;
}

void FileSyncService::watch_loop() {
  while (running_) {
    for (const auto& rule : rules_) {
      if (!rule.enabled || is_paused(rule.id)) {
        continue;
      }

      // Only local->remote directions push on local file changes. Pull-only
      // rules are handled by an explicit sync_now, never by this loop: pushing
      // them here would make downloaded files trigger a send-back loop.
      const bool push = rule.direction == SyncDirection::LocalToRemote ||
                        rule.direction == SyncDirection::Bidirectional;
      if (!push) {
        continue;
      }

      std::error_code error;
      if (!std::filesystem::exists(rule.local_path, error)) {
        continue;
      }

      const auto address = parse_address(rule.remote_address);
      if (!address.has_value()) {
        continue;
      }

      for (const auto& file : collect_files(rule)) {
        const auto modified = std::filesystem::last_write_time(file, error).time_since_epoch().count();
        const auto size = std::filesystem::file_size(file, error);

        const std::string key = rule.id + "\n" + file.string();
        const auto previous = watched_.find(key);
        if (previous != watched_.end() && previous->second.first == modified &&
            previous->second.second == size) {
          continue;
        }
        watched_[key] = {modified, size};

        SyncEvent event;
        event.rule_id = rule.id;
        event.path = file;
        event.event_type = "modified";
        event.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch()).count();
        events_.push_back(event);
        if (callback_) {
          callback_(event);
        }

        // Actually transfer the changed file to the peer's inbox.
        std::error_code rel_error;
        auto relative = std::filesystem::relative(file, rule.local_path, rel_error);
        const std::string remote_name = rel_error ? file.filename().string() : relative.string();
        const auto sent = core::put_inbox_file(
          address->first, address->second, file, remote_name, rule.remote_token,
          &runner_, tls_config_);
        if (sent.has_value()) {
          SyncEvent sent_event;
          sent_event.rule_id = rule.id;
          sent_event.path = file;
          sent_event.event_type = "sent";
          sent_event.timestamp = event.timestamp;
          events_.push_back(sent_event);
          if (callback_) {
            callback_(sent_event);
          }
        }
      }
    }
    std::this_thread::sleep_for(std::chrono::seconds(5));
  }
}

}  // namespace hyprshare::sync
