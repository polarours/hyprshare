#pragma once

#include <atomic>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "hyprshare/platform/process.hpp"
#include "hyprshare/tls/tls_transport.hpp"

namespace hyprshare::sync {

enum class SyncDirection {
  LocalToRemote,
  RemoteToLocal,
  Bidirectional,
};

enum class SyncStatus {
  Idle,
  Syncing,
  Paused,
  Error,
};

struct SyncRule {
  std::string id;
  std::string name;
  std::filesystem::path local_path;
  std::string remote_device_id;
  // The address of the peer, as host:port. The peer runs an inbox that
  // accepts the files.
  std::string remote_address;
  // The token the peer's inbox requires, if any.
  std::string remote_token;
  std::filesystem::path remote_path;
  SyncDirection direction = SyncDirection::LocalToRemote;
  bool enabled = true;
};

struct SyncEvent {
  std::string rule_id;
  std::filesystem::path path;
  std::string event_type;
  int64_t timestamp = 0;
};

using SyncCallback = std::function<void(const SyncEvent&)>;

// The outcome of one synchronisation run.
struct SyncReport {
  std::size_t files_sent = 0;
  std::uintmax_t bytes_sent = 0;
  std::size_t files_received = 0;
  std::uintmax_t bytes_received = 0;
  std::vector<std::string> failures;
};

class FileSyncService {
 public:
  FileSyncService();
  ~FileSyncService();

  std::expected<void, std::string> start();
  void stop();

  std::expected<void, std::string> add_rule(const SyncRule& rule);
  std::expected<void, std::string> remove_rule(const std::string& rule_id);
  std::vector<SyncRule> list_rules() const;

  std::expected<SyncReport, std::string> sync_now(const std::string& rule_id);
  std::expected<void, std::string> pause(const std::string& rule_id);
  std::expected<void, std::string> resume(const std::string& rule_id);

  void set_sync_callback(SyncCallback callback);
  void set_tls_config(const tls::TlsTransportConfig& config);

  SyncStatus status() const;
  std::vector<SyncEvent> recent_events() const;
  bool is_running() const;

 private:
  void watch_loop();
  // Transfers the files of a rule to the peer and records the result.
  SyncReport sync_rule(const SyncRule& rule);
  // The files of the rule, relative to the local path.
  std::vector<std::filesystem::path> collect_files(const SyncRule& rule) const;

  SyncStatus status_ = SyncStatus::Idle;
  std::vector<SyncRule> rules_;
  std::vector<SyncEvent> events_;
  SyncCallback callback_;
  std::atomic<bool> running_{false};

  // The state of every watched file, so that only real changes are sent.
  std::map<std::string, std::pair<std::int64_t, std::uintmax_t>> watched_;
  platform::ProcessRunner runner_;
  tls::TlsTransportConfig tls_config_;
};

}  // namespace hyprshare::sync
