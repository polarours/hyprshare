#pragma once

#include <expected>
#include <optional>
#include <filesystem>

#include "hyprshare/core/config.hpp"
#include "hyprshare/model/types.hpp"
#include "hyprshare/platform/paths.hpp"
#include "hyprshare/platform/process.hpp"
#include "hyprshare/tls/tls_transport.hpp"

namespace hyprshare::core {

std::expected<model::FileShareManifest, std::string> build_file_manifest(const std::filesystem::path& path);
std::expected<model::FileShareManifest, std::string> build_folder_manifest(
  const std::filesystem::path& path,
  const FilesConfig& config);
std::expected<model::StagedFileShare, std::string> stage_file_share(
  const model::FileShareManifest& manifest,
  const platform::Paths& paths,
  const platform::ProcessRunner& runner);
std::expected<std::vector<model::StagedFileShare>, std::string> list_staged_file_shares(
  const platform::Paths& paths);
std::expected<model::StagedFileShare, std::string> load_staged_file_share(
  std::string_view session_id,
  const platform::Paths& paths);
std::expected<std::filesystem::path, std::string> delete_staged_file_share(
  std::string_view session_id,
  const platform::Paths& paths);
std::expected<std::vector<std::filesystem::path>, std::string> prune_staged_file_shares(
  const platform::Paths& paths,
  std::optional<std::uint32_t> older_than_seconds);
std::expected<void, std::string> verify_staged_file_share(
  const model::StagedFileShare& staged,
  const platform::ProcessRunner& runner);
std::expected<model::RemoteFileShareOffer, std::string> inspect_remote_file_share(
  std::string_view host,
  std::uint16_t port,
  std::string_view access_token = {},
  std::string_view peer_id = {},
  const tls::TlsTransportConfig& tls_config = {});
std::expected<void, std::string> probe_remote_file_share(
  std::string_view host,
  std::uint16_t port,
  std::string_view access_token = {},
  std::string_view peer_id = {},
  const tls::TlsTransportConfig& tls_config = {});
std::expected<std::uintmax_t, std::string> serve_staged_file_share(
  const model::StagedFileShare& staged,
  std::string_view bind_host,
  std::uint16_t port,
  const platform::Paths& paths,
  const platform::ProcessRunner& runner,
  std::string_view access_token = {},
  std::uint32_t token_ttl_seconds = 0,
  std::string_view allowed_peer_id = {},
  std::uint32_t idle_timeout_seconds = 30,
  const tls::TlsTransportConfig& tls_config = {});
std::expected<model::StagedFileShare, std::string> fetch_staged_file_share(
  std::string_view host,
  std::uint16_t port,
  const platform::Paths& paths,
  const platform::ProcessRunner& runner,
  std::string_view access_token = {},
  std::string_view peer_id = {},
  const tls::TlsTransportConfig& tls_config = {});
std::expected<std::filesystem::path, std::string> export_staged_file_share(
  const model::StagedFileShare& staged,
  const std::filesystem::path& bundle_path,
  const platform::ProcessRunner& runner);
std::expected<model::StagedFileShare, std::string> import_staged_file_share(
  const std::filesystem::path& bundle_path,
  const platform::Paths& paths,
  const platform::ProcessRunner& runner);
std::expected<model::ReceivedFileShare, std::string> receive_staged_file_share(
  const model::StagedFileShare& staged,
  const std::filesystem::path& destination_base);

}  // namespace hyprshare::core
