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

// Escape an arbitrary string for embedding in a hand-written JSON response.
// CLI values (paths, names, endpoints) come from the user and may contain
// quotes, backslashes or newlines.
inline std::string json_escape(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (const char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out.push_back(c); break;
    }
  }
  return out;
}

void print_status_json(
  const hyprshare::platform::Paths& paths,
  const hyprshare::model::DoctorReport& report,
  const hyprshare::core::BackendRegistry& backends,
  const hyprshare::core::AdapterRegistry& adapters);

void print_discovery_status_json(const hyprshare::core::DiscoveryStatus& status);

void print_discovered_inboxes_json(const std::vector<hyprshare::core::DiscoveredInbox>& inboxes);

void print_backends_json(
  const hyprshare::core::BackendRegistry& backends,
  const hyprshare::model::DoctorReport& report);

void print_profile_json(const hyprshare::model::AppProfile& profile);

void print_doctor_json(const hyprshare::model::DoctorReport& report);

void print_config_json(const hyprshare::core::Config& config, const hyprshare::platform::Paths& paths);

void print_clipboard_text_json(const hyprshare::model::ClipboardText& clipboard);

void print_inbox_status_json(
  const hyprshare::core::Config& config,
  const std::filesystem::path& resolved_receive_dir);

void print_inbox_open_json(
  const std::filesystem::path& receive_dir,
  const std::string_view command,
  const bool dry_run_mode);

void print_inbox_announce_json(
  const hyprshare::core::InboxAnnouncePlan& plan,
  const std::optional<int> pid,
  const bool dry_run_mode);

void print_inbox_history_json(const std::vector<hyprshare::model::InboxHistoryEntry>& entries);

void print_cleared_inbox_history_json(const std::size_t removed_count);

void print_inbox_received_json(const hyprshare::model::InboxReceivedFile& received);

void print_inbox_received_list_json(const std::vector<hyprshare::model::InboxReceivedFile>& received_files);

void print_inbox_put_json(
  const std::string_view host,
  const std::uint16_t port,
  const std::filesystem::path& file_path,
  const hyprshare::core::InboxUploadResult& upload);

void print_inbox_put_batch_json(
  const std::string_view host,
  const std::uint16_t port,
  const std::vector<std::filesystem::path>& file_paths,
  const std::vector<hyprshare::core::InboxUploadResult>& uploads);

void print_inbox_extract_json(const hyprshare::core::InboxExtractResult& result);

void print_inbox_received_with_extract_json(
  const std::vector<hyprshare::model::InboxReceivedFile>& received_files,
  const std::vector<hyprshare::core::InboxExtractResult>& extracted_archives);

void print_device_identity_json(const hyprshare::model::DeviceIdentity& identity);

void print_trusted_peers_json(const std::vector<hyprshare::model::TrustedPeer>& peers);

void print_trusted_peer_json(const hyprshare::model::TrustedPeer& peer);

void print_exported_peer_json(
  const hyprshare::model::DeviceIdentity& identity,
  const std::filesystem::path& export_path);

void print_peer_invite_json(const hyprshare::model::PeerInvite& invite);

void print_written_peer_invite_json(
  const hyprshare::model::PeerInvite& invite,
  const std::filesystem::path& output_path);

void print_removed_peer_json(
  const std::string_view device_id,
  const std::filesystem::path& peer_path);

void print_plan_json(const hyprshare::model::SharePlan& plan, const bool dry_run);

void print_launch_plan_json(const hyprshare::model::AppLaunchPlan& plan, const bool dry_run);

void print_file_manifest_json(const hyprshare::model::FileShareManifest& manifest);

void print_staged_share_json(const hyprshare::model::StagedFileShare& staged);

void print_session_list_json(const std::vector<hyprshare::model::StagedFileShare>& sessions);

void print_deleted_session_json(
  const std::string_view session_id,
  const std::filesystem::path& stage_path);

void print_pruned_sessions_json(const std::vector<std::filesystem::path>& pruned_sessions);

void print_exported_share_json(
  const std::string_view session_id,
  const std::filesystem::path& stage_path,
  const std::filesystem::path& bundle_path);

void print_remote_offer_json(const hyprshare::model::RemoteFileShareOffer& offer);

void print_probe_json(
  const std::string_view host,
  const std::uint16_t port,
  const bool reachable,
  const std::string_view error);

void print_served_share_json(
  const std::string_view session_id,
  const std::string_view host,
  const std::uint16_t port,
  const std::uintmax_t bytes_sent);

void print_verified_share_json(const hyprshare::model::StagedFileShare& staged);

void print_received_share_json(const hyprshare::model::ReceivedFileShare& received);

void print_support_bundle_json(const hyprshare::diagnostics::SupportBundle& bundle);

}  // namespace hyprshare::cli
