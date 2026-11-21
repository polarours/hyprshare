#include "hyprshare/cloud/cloud_sync.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

#include "hyprshare/http/http_client.hpp"
#include "hyprshare/util/string.hpp"

namespace hyprshare::cloud {

namespace {

std::filesystem::path cloud_root() {
  return std::filesystem::path(getenv("HOME")) / ".local/state/hyprshare/cloud";
}

std::filesystem::path state_dir(const std::string& provider) {
  return cloud_root() / provider;
}

std::string read_value(const std::string& line, const std::string& key) {
  const std::size_t equals = line.find('=');
  if (equals == std::string::npos) {
    return "";
  }
  if (util::trim(line.substr(0, equals)) != key) {
    return "";
  }
  return util::strip_quotes(util::trim(line.substr(equals + 1)));
}

// Encodes a remote path so that it can be used as the path of a URL.
std::string encode_remote_path(const std::string& remote_path) {
  std::string encoded;
  for (const char character : remote_path) {
    if (character == '/') {
      encoded += '/';
    } else {
      encoded += http::url_encode(std::string(1, character));
    }
  }
  return encoded;
}

}  // namespace

CloudSyncClient::CloudSyncClient() {}

CloudSyncClient::~CloudSyncClient() {}

std::expected<void, std::string> CloudSyncClient::configure(const CloudConfig& config) {
  config_ = config;
  if (!config.enabled) return {};

  std::error_code ec;
  std::filesystem::create_directories(state_dir(config.provider), ec);
  if (ec) return std::unexpected("failed to create state directory: " + ec.message());

  std::ofstream meta(state_dir(config.provider) / "config.conf");
  if (!meta.is_open()) return std::unexpected("failed to write config metadata");
  meta << "# hyprshare cloud provider configuration\n\n";
  meta << "provider = \"" << config.provider << "\"\n";
  meta << "type = \"" << config.type << "\"\n";
  meta << "endpoint = \"" << config.endpoint << "\"\n";
  meta << "bucket = \"" << config.bucket << "\"\n";
  meta << "access_key = \"" << config.access_key << "\"\n";
  meta << "secret_key = \"" << config.secret_key << "\"\n";
  return {};
}

std::expected<void, std::string> CloudSyncClient::load(const std::string& provider) {
  std::string target = provider;
  if (target.empty()) {
    const auto providers = configured_providers();
    if (providers.empty()) {
      return std::unexpected(
        "no cloud provider configured; run `hyprshare cloud configure <provider>` first");
    }
    target = providers.back();
  }

  const auto meta_path = state_dir(target) / "config.conf";
  std::ifstream meta(meta_path);
  if (!meta.is_open()) {
    return std::unexpected("cloud provider not configured: " + target);
  }

  CloudConfig config;
  config.provider = target;
  config.enabled = true;

  std::string line;
  while (std::getline(meta, line)) {
    if (const auto value = read_value(line, "type"); !value.empty()) {
      config.type = value;
      continue;
    }
    if (const auto value = read_value(line, "endpoint"); !value.empty()) {
      config.endpoint = value;
      continue;
    }
    if (const auto value = read_value(line, "bucket"); !value.empty()) {
      config.bucket = value;
      continue;
    }
    if (const auto value = read_value(line, "access_key"); !value.empty()) {
      config.access_key = value;
      continue;
    }
    if (const auto value = read_value(line, "secret_key"); !value.empty()) {
      config.secret_key = value;
    }
  }

  config_ = config;
  return {};
}

std::vector<std::string> CloudSyncClient::configured_providers() const {
  std::vector<std::string> providers;
  std::error_code ec;
  if (!std::filesystem::exists(cloud_root(), ec)) {
    return providers;
  }
  for (const auto& entry : std::filesystem::directory_iterator(cloud_root(), ec)) {
    if (entry.is_directory() && std::filesystem::exists(entry.path() / "config.conf")) {
      providers.push_back(entry.path().filename().string());
    }
  }
  std::sort(providers.begin(), providers.end());
  return providers;
}

std::filesystem::path CloudSyncClient::local_store() const {
  return state_dir(config_.provider);
}

std::string CloudSyncClient::object_url(const std::string& remote_path) const {
  if (config_.type != kHttpProvider) {
    return "";
  }

  std::string base = config_.endpoint;
  while (!base.empty() && base.back() == '/') {
    base.pop_back();
  }
  if (!config_.bucket.empty()) {
    base += "/" + config_.bucket;
  }
  if (remote_path.empty()) {
    return base + "/";
  }
  return base + "/" + encode_remote_path(remote_path);
}

void CloudSyncClient::remember_key(const std::string& key) {
  auto keys = known_keys();
  if (std::find(keys.begin(), keys.end(), key) != keys.end()) {
    return;
  }
  keys.push_back(key);

  const auto index_path = local_store() / "index.list";
  std::ofstream output(index_path, std::ios::trunc);
  if (!output.is_open()) {
    return;
  }
  for (const auto& known : keys) {
    output << known << "\n";
  }
}

std::vector<std::string> CloudSyncClient::known_keys() const {
  std::vector<std::string> keys;
  const auto index_path = local_store() / "index.list";
  std::ifstream input(index_path);
  if (!input.is_open()) {
    return keys;
  }
  std::string line;
  while (std::getline(input, line)) {
    const std::string trimmed = util::trim(line);
    if (!trimmed.empty()) {
      keys.push_back(trimmed);
    }
  }
  return keys;
}

std::expected<void, std::string> CloudSyncClient::upload_local(
  const std::filesystem::path& local_path, const std::string& remote_path) {
  const auto target = local_store() / remote_path;
  std::error_code ec;
  std::filesystem::create_directories(target.parent_path(), ec);
  if (ec) {
    return std::unexpected("failed to create the remote directory: " + ec.message());
  }
  std::filesystem::copy_file(local_path, target,
    std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    return std::unexpected("failed to copy the file into the store: " + ec.message());
  }
  remember_key(remote_path);
  return {};
}

std::expected<void, std::string> CloudSyncClient::upload_http(
  const std::filesystem::path& local_path, const std::string& remote_path) {
  if (config_.endpoint.empty()) {
    return std::unexpected("the http provider needs an endpoint; reconfigure it with "
      "`hyprshare cloud configure " + config_.provider + " --endpoint <url>`");
  }

  const auto url = object_url(remote_path);
  http::RequestOptions options;
  options.bearer_token = config_.access_key;
  options.basic_auth = config_.secret_key;

  http::Client client;
  const auto response = client.put_file(url, local_path, options);
  if (!response.has_value()) {
    return std::unexpected(response.error());
  }
  if (response->status < 200 || response->status >= 300) {
    return std::unexpected("the server rejected the upload with status " +
      std::to_string(response->status));
  }

  remember_key(remote_path);
  return {};
}

std::expected<void, std::string> CloudSyncClient::upload(
  const std::filesystem::path& local_path, const std::string& remote_path) {
  std::error_code ec;
  if (!std::filesystem::exists(local_path, ec)) {
    set_status(SyncStatus::Error, local_path.string());
    return std::unexpected("source not found: " + local_path.string());
  }
  const auto total = std::filesystem::file_size(local_path, ec);
  set_status(SyncStatus::Uploading, local_path.string(), 0, total);
  auto result = config_.type == kHttpProvider
    ? upload_http(local_path, remote_path)
    : upload_local(local_path, remote_path);
  if (!result.has_value()) {
    set_status(SyncStatus::Error, local_path.string());
    return result;
  }
  set_status(SyncStatus::Idle, local_path.string(), total, total);
  return {};
}

std::expected<void, std::string> CloudSyncClient::download_local(
  const std::string& remote_path, const std::filesystem::path& local_path) {
  const auto source = local_store() / remote_path;
  std::error_code ec;
  if (!std::filesystem::exists(source, ec)) {
    return std::unexpected("object not found: " + remote_path);
  }
  if (local_path.has_parent_path()) {
    std::filesystem::create_directories(local_path.parent_path(), ec);
  }
  std::filesystem::copy_file(source, local_path,
    std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    return std::unexpected("failed to copy the file out of the store: " + ec.message());
  }
  return {};
}

std::expected<void, std::string> CloudSyncClient::download_http(
  const std::string& remote_path, const std::filesystem::path& local_path) {
  if (config_.endpoint.empty()) {
    return std::unexpected("the http provider needs an endpoint; reconfigure it with "
      "`hyprshare cloud configure " + config_.provider + " --endpoint <url>`");
  }

  http::RequestOptions options;
  options.bearer_token = config_.access_key;
  options.basic_auth = config_.secret_key;

  http::Client client;
  const auto response = client.get(object_url(remote_path), options);
  if (!response.has_value()) {
    return std::unexpected(response.error());
  }
  if (response->status == 404) {
    return std::unexpected("object not found: " + remote_path);
  }
  if (response->status < 200 || response->status >= 300) {
    return std::unexpected("the server rejected the download with status " +
      std::to_string(response->status));
  }

  std::error_code ec;
  if (local_path.has_parent_path()) {
    std::filesystem::create_directories(local_path.parent_path(), ec);
  }
  std::ofstream output(local_path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    return std::unexpected("failed to open the destination file: " + local_path.string());
  }
  output << response->body;
  if (output.fail()) {
    return std::unexpected("failed to write the destination file: " + local_path.string());
  }

  remember_key(remote_path);
  return {};
}

std::expected<void, std::string> CloudSyncClient::download(
  const std::string& remote_path, const std::filesystem::path& local_path) {
  set_status(SyncStatus::Downloading, remote_path);
  auto result = config_.type == kHttpProvider
    ? download_http(remote_path, local_path)
    : download_local(remote_path, local_path);
  if (!result.has_value()) {
    set_status(SyncStatus::Error, remote_path);
    return result;
  }
  std::error_code ec;
  const auto total = std::filesystem::exists(local_path, ec)
    ? std::filesystem::file_size(local_path, ec) : 0;
  set_status(SyncStatus::Idle, remote_path, total, total);
  return {};
}

std::expected<void, std::string> CloudSyncClient::sync_directory(
  const std::filesystem::path& local_dir, const std::string& remote_dir) {
  std::error_code ec;
  if (!std::filesystem::exists(local_dir, ec)) {
    return std::unexpected("local directory not found: " + local_dir.string());
  }

  std::size_t transferred = 0;
  set_status(SyncStatus::Syncing, local_dir.string());
  for (const auto& entry : std::filesystem::recursive_directory_iterator(local_dir, ec)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto relative = std::filesystem::relative(entry.path(), local_dir, ec);
    if (ec) {
      continue;
    }
    const auto key = remote_dir.empty() ? relative.string() : remote_dir + "/" + relative.string();
    const auto result = upload(entry.path(), key);
    if (!result.has_value()) {
      return std::unexpected("failed to upload " + relative.string() + ": " + result.error());
    }
    ++transferred;
  }

  if (transferred == 0) {
    set_status(SyncStatus::Error, local_dir.string());
    return std::unexpected("no files were found in " + local_dir.string());
  }
  set_status(SyncStatus::Idle, local_dir.string(), transferred, transferred);
  return {};
}

std::expected<std::vector<std::string>, std::string> CloudSyncClient::list_files(
  const std::string& remote_dir) {
  std::vector<std::string> files;

  if (config_.type == kHttpProvider) {
    // A plain HTTP store has no listing interface, so the keys this client
    // transferred are reported, and each one is checked against the server.
    http::RequestOptions options;
    options.bearer_token = config_.access_key;
    options.basic_auth = config_.secret_key;

    http::Client client;
    for (const auto& key : known_keys()) {
      if (!remote_dir.empty() && key.rfind(remote_dir + "/", 0) != 0) {
        continue;
      }
      const auto response = client.head(object_url(key), options);
      if (response.has_value() && response->status >= 200 && response->status < 300) {
        files.push_back(key);
      }
    }
    return files;
  }

  const auto root = local_store() / remote_dir;
  std::error_code ec;
  if (!std::filesystem::exists(root, ec)) {
    return files;
  }
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto relative = std::filesystem::relative(entry.path(), local_store(), ec);
    if (!ec) {
      files.push_back(relative.string());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

void CloudSyncClient::set_status(SyncStatus status, const std::string& file_path,
  std::uint64_t bytes, std::uint64_t total) {
  status_ = status;
  if (callback_) {
    SyncProgress progress;
    progress.file_path = file_path;
    progress.bytes_transferred = bytes;
    progress.total_bytes = total;
    progress.percentage = total > 0 ? 100.0 * static_cast<double>(bytes) / total : 0.0;
    progress.status = status;
    callback_(progress);
  }
}

void CloudSyncClient::set_sync_callback(SyncCallback callback) {
  callback_ = std::move(callback);
}

SyncStatus CloudSyncClient::status() const {
  return status_;
}

CloudConfig CloudSyncClient::config() const {
  return config_;
}

}  // namespace hyprshare::cloud
