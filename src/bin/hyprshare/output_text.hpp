#pragma once

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "hyprshare/core/adapter_registry.hpp"
#include "hyprshare/core/backend_registry.hpp"
#include "hyprshare/core/config.hpp"
#include "hyprshare/core/discovery.hpp"
#include "hyprshare/core/inbox.hpp"
#include "hyprshare/diagnostics/support_bundle.hpp"
#include "hyprshare/model/types.hpp"
#include "hyprshare/platform/paths.hpp"

namespace hyprshare::cli {

bool path_has_write_bit(const std::filesystem::path& path);
std::vector<std::string> inbox_advertised_hosts(const std::string_view bind_host);

void print_status_text(
  const hyprshare::platform::Paths& paths,
  const hyprshare::model::DoctorReport& report,
  const hyprshare::core::BackendRegistry& backends,
  const hyprshare::core::AdapterRegistry& adapters);

void print_discovery_status_text(const hyprshare::core::DiscoveryStatus& status);

void print_discovered_inboxes_text(const std::vector<hyprshare::core::DiscoveredInbox>& inboxes);

void print_backends_text(
  const hyprshare::core::BackendRegistry& backends,
  const hyprshare::model::DoctorReport& report);

void print_profile_text(const hyprshare::model::AppProfile& profile);

void print_doctor_text(const hyprshare::model::DoctorReport& report);

void print_config_text(const hyprshare::core::Config& config, const hyprshare::platform::Paths& paths);

void print_clipboard_summary_text(const hyprshare::model::ClipboardText& clipboard);

void print_clipboard_write_text(const hyprshare::model::ClipboardText& clipboard);

void print_inbox_status_text(
  const hyprshare::core::Config& config,
  const std::filesystem::path& resolved_receive_dir);

void print_inbox_open_text(
  const std::filesystem::path& receive_dir,
  const std::string_view command,
  const bool dry_run_mode);

void print_inbox_announce_text(
  const hyprshare::core::InboxAnnouncePlan& plan,
  const std::optional<int> pid,
  const bool dry_run_mode);

void print_inbox_history_text(const std::vector<hyprshare::model::InboxHistoryEntry>& entries);

void print_cleared_inbox_history_text(const std::size_t removed_count);

void print_inbox_received_text(const hyprshare::model::InboxReceivedFile& received);

void print_inbox_received_list_text(const std::vector<hyprshare::model::InboxReceivedFile>& received_files);

void print_inbox_put_text(
  const std::string_view host,
  const std::uint16_t port,
  const std::filesystem::path& file_path,
  const hyprshare::core::InboxUploadResult& upload);

void print_inbox_put_batch_text(
  const std::string_view host,
  const std::uint16_t port,
  const std::vector<std::filesystem::path>& file_paths,
  const std::vector<hyprshare::core::InboxUploadResult>& uploads);

void print_inbox_extract_text(const hyprshare::core::InboxExtractResult& result);

void print_inbox_auto_extract_text(const std::vector<hyprshare::core::InboxExtractResult>& extracted_archives);

void print_device_identity_text(const hyprshare::model::DeviceIdentity& identity);

void print_trusted_peers_text(const std::vector<hyprshare::model::TrustedPeer>& peers);

void print_trusted_peer_text(const hyprshare::model::TrustedPeer& peer);

void print_exported_peer_text(
  const hyprshare::model::DeviceIdentity& identity,
  const std::filesystem::path& export_path);

void print_peer_invite_text(const hyprshare::model::PeerInvite& invite);

void print_written_peer_invite_text(
  const hyprshare::model::PeerInvite& invite,
  const std::filesystem::path& output_path);

void print_removed_peer_text(
  const std::string_view device_id,
  const std::filesystem::path& peer_path);

void print_plan_text(const hyprshare::model::SharePlan& plan, const bool dry_run);

void print_launch_plan_text(const hyprshare::model::AppLaunchPlan& plan, const bool dry_run);

void print_file_manifest_text(const hyprshare::model::FileShareManifest& manifest);

void print_staged_share_text(const hyprshare::model::StagedFileShare& staged);

void print_session_list_text(const std::vector<hyprshare::model::StagedFileShare>& sessions);

void print_deleted_session_text(
  const std::string_view session_id,
  const std::filesystem::path& stage_path);

void print_pruned_sessions_text(const std::vector<std::filesystem::path>& pruned_sessions);

void print_exported_share_text(
  const std::string_view session_id,
  const std::filesystem::path& stage_path,
  const std::filesystem::path& bundle_path);

void print_remote_offer_text(const hyprshare::model::RemoteFileShareOffer& offer);

void print_probe_text(
  const std::string_view host,
  const std::uint16_t port,
  const bool reachable,
  const std::string_view error);

void print_served_share_text(
  const std::string_view session_id,
  const std::string_view host,
  const std::uint16_t port,
  const std::uintmax_t bytes_sent);

void print_verified_share_text(const hyprshare::model::StagedFileShare& staged);

void print_received_share_text(const hyprshare::model::ReceivedFileShare& received);

void print_support_bundle_text(const hyprshare::diagnostics::SupportBundle& bundle);

void print_inbox_serve_hint(
  std::string_view host,
  std::uint16_t port,
  const std::filesystem::path& destination,
  std::uint32_t upload_count,
  std::uint32_t idle_timeout_seconds,
  std::uintmax_t max_upload_bytes,
  std::string_view token);

void print_session_serve_token_hint(
  std::string_view host,
  std::uint16_t port,
  std::string_view token,
  std::uint32_t token_ttl_seconds,
  std::string_view allowed_peer_id);

}  // namespace hyprshare::cli
