#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace hyprshare::cloud {

enum class SyncStatus {
  Idle,
  Syncing,
  Uploading,
  Downloading,
  Error
};

// The two storage backends the client knows about.
//
//   local - objects live in a directory below the state directory
//   http  - objects live on an HTTP server, addressed as
//           <endpoint>/<bucket>/<key>
inline constexpr const char* kLocalProvider = "local";
inline constexpr const char* kHttpProvider = "http";

struct CloudConfig {
  // The name of the provider, used for the directory below the state
  // directory. It is independent of the backend type.
  std::string provider;
  // "local" or "http".
  std::string type = kLocalProvider;
  // The base URL of an HTTP storage server.
  std::string endpoint;
  // The bucket or container on the server.
  std::string bucket;
  // Sent as a bearer token.
  std::string access_key;
  // Sent as HTTP basic authentication.
  std::string secret_key;
  bool enabled = false;
};

struct SyncProgress {
  std::string file_path;
  std::uint64_t bytes_transferred = 0;
  std::uint64_t total_bytes = 0;
  double percentage = 0;
  SyncStatus status = SyncStatus::Idle;
};

using SyncCallback = std::function<void(const SyncProgress&)>;

class CloudSyncClient {
 public:
  CloudSyncClient();
  ~CloudSyncClient();

  std::expected<void, std::string> configure(const CloudConfig& config);

  // Loads the persisted configuration. An empty provider selects the most
  // recently configured one.
  std::expected<void, std::string> load(const std::string& provider = "");

  // The providers that have a persisted configuration.
  std::vector<std::string> configured_providers() const;

  // The directory that backs the configured provider. For the HTTP backend
  // this is where the key index is kept.
  std::filesystem::path local_store() const;

  // The URL an object is stored at. Only meaningful for the HTTP backend.
  std::string object_url(const std::string& remote_path) const;

  std::expected<void, std::string> upload(const std::filesystem::path& local_path, const std::string& remote_path);
  std::expected<void, std::string> download(const std::string& remote_path, const std::filesystem::path& local_path);
  std::expected<void, std::string> sync_directory(const std::filesystem::path& local_dir, const std::string& remote_dir);
  std::expected<std::vector<std::string>, std::string> list_files(const std::string& remote_dir);

  void set_sync_callback(SyncCallback callback);

  SyncStatus status() const;
  CloudConfig config() const;

 private:
  std::expected<void, std::string> upload_local(const std::filesystem::path& local_path, const std::string& remote_path);
  std::expected<void, std::string> upload_http(const std::filesystem::path& local_path, const std::string& remote_path);
  std::expected<void, std::string> download_local(const std::string& remote_path, const std::filesystem::path& local_path);
  std::expected<void, std::string> download_http(const std::string& remote_path, const std::filesystem::path& local_path);

  // The keys this client has transferred, so that the listing can report them
  // even when the server has no listing interface.
  void remember_key(const std::string& key);
  std::vector<std::string> known_keys() const;

  CloudConfig config_;
  SyncStatus status_ = SyncStatus::Idle;
  SyncCallback callback_;
};

}  // namespace hyprshare::cloud
