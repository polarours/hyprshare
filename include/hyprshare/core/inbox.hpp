#pragma once

#include <expected>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "hyprshare/model/types.hpp"
#include "hyprshare/tls/tls_transport.hpp"

namespace hyprshare::platform {
class ProcessRunner;
}  // namespace hyprshare::platform

namespace hyprshare::core {

struct InboxUploadResult {
  std::uintmax_t bytes_sent = 0;
  std::filesystem::path uploaded_path;
  std::string remote_name;
  bool archived_directory = false;
  std::string sha256;
};

struct InboxArchiveInspection {
  std::filesystem::path archive_path;
  std::string top_level;
  std::vector<std::string> entries;
};

struct InboxExtractResult {
  InboxArchiveInspection inspection;
  std::filesystem::path destination_dir;
  std::filesystem::path extracted_path;
  bool dry_run = false;
};

std::expected<model::InboxReceivedFile, std::string> serve_inbox_once(
  std::string_view bind_host,
  std::uint16_t port,
  const std::filesystem::path& destination_dir,
  std::string_view access_token = {},
  std::uint32_t idle_timeout_seconds = 0,
  std::uintmax_t max_upload_bytes = 0,
  const platform::ProcessRunner* checksum_runner = nullptr,
  const tls::TlsTransportConfig& tls_config = {});
std::expected<std::vector<model::InboxReceivedFile>, std::string> serve_inbox(
  std::string_view bind_host,
  std::uint16_t port,
  const std::filesystem::path& destination_dir,
  uint32_t upload_count,
  std::string_view access_token = {},
  uint32_t idle_timeout_seconds = 0,
  std::uintmax_t max_upload_bytes = 0,
  const platform::ProcessRunner* checksum_runner = nullptr,
  const tls::TlsTransportConfig& tls_config = {},
  // When a stop flag is given, the server keeps the port and returns as soon
  // as the flag is set.
  const std::atomic<bool>* stop = nullptr);

// Reports how much of an upload has been sent.
using UploadProgressCallback = std::function<void(std::uint64_t sent, std::uint64_t total)>;

std::expected<std::uintmax_t, std::string> put_inbox_file(
  std::string_view host,
  std::uint16_t port,
  const std::filesystem::path& file_path,
  std::string_view remote_name = {},
  std::string_view access_token = {},
  const platform::ProcessRunner* checksum_runner = nullptr,
  const tls::TlsTransportConfig& tls_config = {},
  const UploadProgressCallback* progress = nullptr);

// A file that a peer inbox holds.
struct InboxRemoteFile {
  std::string name;
  std::uintmax_t bytes = 0;
  std::int64_t modified_at_unix = 0;
};

// Asks a peer inbox which files it holds.
std::expected<std::vector<InboxRemoteFile>, std::string> list_inbox_files(
  std::string_view host,
  std::uint16_t port,
  std::string_view access_token = {},
  const tls::TlsTransportConfig& tls_config = {});

// Downloads one file that a peer inbox holds.
std::expected<std::uintmax_t, std::string> get_inbox_file(
  std::string_view host,
  std::uint16_t port,
  const std::string& remote_name,
  const std::filesystem::path& destination,
  std::string_view access_token = {},
  const tls::TlsTransportConfig& tls_config = {});

std::expected<InboxUploadResult, std::string> put_inbox_path(
  std::string_view host,
  std::uint16_t port,
  const std::filesystem::path& path,
  const platform::ProcessRunner& runner,
  std::string_view remote_name = {},
  std::string_view access_token = {},
  const tls::TlsTransportConfig& tls_config = {},
  const UploadProgressCallback* progress = nullptr);

std::expected<InboxArchiveInspection, std::string> inspect_inbox_archive(
  const std::filesystem::path& archive_path,
  const platform::ProcessRunner& runner);
std::expected<InboxExtractResult, std::string> extract_inbox_archive(
  const std::filesystem::path& archive_path,
  const std::filesystem::path& destination_dir,
  const platform::ProcessRunner& runner,
  bool dry_run = false);

}  // namespace hyprshare::core
