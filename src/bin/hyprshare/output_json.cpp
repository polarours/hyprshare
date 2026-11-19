#include "output_json.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "hyprshare/core/adapter_registry.hpp"
#include "hyprshare/core/backend_registry.hpp"
#include "hyprshare/core/config.hpp"
#include "hyprshare/core/discovery.hpp"
#include "hyprshare/core/inbox.hpp"
#include "hyprshare/core/inbox_history.hpp"
#include "hyprshare/diagnostics/doctor.hpp"
#include "hyprshare/diagnostics/support_bundle.hpp"
#include "hyprshare/model/types.hpp"
#include "hyprshare/platform/paths.hpp"
#include "hyprshare/util/string.hpp"

#include "output_text.hpp"

namespace {

using hyprshare::model::FileShareManifest;

std::string bool_json(const bool value) { return value ? "true" : "false"; }

std::string string_json(const std::string_view value) { return "\"" + hyprshare::util::escape_json(value) + "\""; }

std::string string_array_json(const std::vector<std::string>& values) {
  std::ostringstream stream;
  stream << "[";
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0) {
      stream << ",";
    }
    stream << string_json(values[index]);
  }
  stream << "]";
  return stream.str();
}

std::string file_manifest_json_string(const FileShareManifest& manifest) {
  std::ostringstream stream;
  stream << "{"
         << "\"capability\":" << string_json(hyprshare::model::to_string(manifest.capability)) << ","
         << "\"share_name\":" << string_json(manifest.share_name) << ","
         << "\"root_path\":" << string_json(manifest.root_path) << ","
         << "\"read_only\":" << bool_json(manifest.read_only) << ","
         << "\"total_bytes\":" << manifest.total_bytes << ","
         << "\"file_count\":" << manifest.file_count << ","
         << "\"directory_count\":" << manifest.directory_count << ","
         << "\"notes\":" << string_array_json(manifest.notes) << ","
         << "\"entries\":[";
  for (std::size_t index = 0; index < manifest.entries.size(); ++index) {
    const auto& entry = manifest.entries[index];
    if (index > 0) {
      stream << ",";
    }
    stream << "{"
           << "\"relative_path\":" << string_json(entry.relative_path) << ","
           << "\"absolute_path\":" << string_json(entry.absolute_path) << ","
           << "\"is_directory\":" << bool_json(entry.is_directory) << ","
           << "\"size_bytes\":" << entry.size_bytes
           << "}";
  }
  stream << "]}";
  return stream.str();
}

std::string inbox_received_json_fragment(const hyprshare::model::InboxReceivedFile& received) {
  std::ostringstream stream;
  stream << "{"
         << "\"file_name\":" << string_json(received.file_name) << ","
         << "\"destination_path\":" << string_json(received.destination_path) << ","
         << "\"bytes_received\":" << received.bytes_received << ","
         << "\"sha256\":" << string_json(received.sha256)
         << "}";
  return stream.str();
}

std::string inbox_extract_json_fragment(const hyprshare::core::InboxExtractResult& result) {
  std::ostringstream stream;
  stream << "{"
         << "\"archive_path\":" << string_json(result.inspection.archive_path.string()) << ","
         << "\"destination_dir\":" << string_json(result.destination_dir.string()) << ","
         << "\"extracted_path\":" << string_json(result.extracted_path.string()) << ","
         << "\"top_level\":" << string_json(result.inspection.top_level) << ","
         << "\"entry_count\":" << result.inspection.entries.size()
         << "}";
  return stream.str();
}

std::string inbox_put_upload_json_fragment(
  const std::filesystem::path& file_path,
  const hyprshare::core::InboxUploadResult& upload) {
  std::ostringstream stream;
  stream << "{"
         << "\"file_path\":" << string_json(file_path.string()) << ","
         << "\"uploaded_path\":" << string_json(upload.uploaded_path.string()) << ","
         << "\"remote_name\":" << string_json(upload.remote_name) << ","
         << "\"archived_directory\":" << bool_json(upload.archived_directory) << ","
         << "\"bytes_sent\":" << upload.bytes_sent << ","
         << "\"sha256\":" << string_json(upload.sha256)
         << "}";
  return stream.str();
}

}  // namespace

namespace hyprshare::cli {

using hyprshare::core::AdapterRegistry;
using hyprshare::core::BackendRegistry;
using hyprshare::core::Config;
using hyprshare::core::DiscoveryStatus;
using hyprshare::core::DiscoveredInbox;
using hyprshare::core::InboxAnnouncePlan;
using hyprshare::core::InboxExtractResult;
using hyprshare::core::InboxUploadResult;
using hyprshare::diagnostics::SupportBundle;
using hyprshare::model::AppLaunchPlan;
using hyprshare::model::AppProfile;
using hyprshare::model::ClipboardText;
using hyprshare::model::DeviceIdentity;
using hyprshare::model::FileShareManifest;
using hyprshare::model::PeerInvite;
using hyprshare::model::ReceivedFileShare;
using hyprshare::model::RemoteFileShareOffer;
using hyprshare::model::SharePlan;
using hyprshare::model::StagedFileShare;
using hyprshare::model::TrustedPeer;
using hyprshare::platform::Paths;

void print_status_json(const Paths& paths, const hyprshare::model::DoctorReport& report, const BackendRegistry& backends, const AdapterRegistry& adapters) {
  std::cout << "{"
            << "\"version\":" << string_json(HYPRSHARE_VERSION) << ","
            << "\"paths\":{"
            << "\"data_dir\":" << string_json(paths.data_dir.string()) << ","
            << "\"config_file\":" << string_json(paths.config_file.string()) << ","
            << "\"state_dir\":" << string_json(paths.state_dir.string())
            << "},"
            << "\"catalogs\":{"
            << "\"backends\":" << backends.all().size() << ","
            << "\"adapters\":" << adapters.all().size()
            << "},"
            << "\"facts\":{";

  bool first = true;
  for (const auto& [fact, value] : report.facts) {
    if (!first) {
      std::cout << ",";
    }
    first = false;
    std::cout << string_json(fact) << ":" << bool_json(value);
  }
  std::cout << "}}\n";
}

void print_discovery_status_json(const DiscoveryStatus& status) {
  std::cout << "{"
            << "\"avahi_publish_service_available\":" << bool_json(status.avahi_publish_service_available) << ","
            << "\"avahi_browse_available\":" << bool_json(status.avahi_browse_available) << ","
            << "\"avahi_daemon_active\":" << bool_json(status.avahi_daemon_active) << ","
            << "\"ready_to_announce\":" << bool_json(status.ready_to_announce) << ","
            << "\"ready_to_browse\":" << bool_json(status.ready_to_browse)
            << "}\n";
}

void print_discovered_inboxes_json(const std::vector<DiscoveredInbox>& inboxes) {
  std::cout << "[";
  for (std::size_t index = 0; index < inboxes.size(); ++index) {
    if (index > 0) {
      std::cout << ",";
    }
    const auto& inbox = inboxes[index];
    std::cout << "{"
              << "\"name\":" << string_json(inbox.name) << ","
              << "\"service_type\":" << string_json(inbox.service_type) << ","
              << "\"domain\":" << string_json(inbox.domain) << ","
              << "\"host\":" << string_json(inbox.host) << ","
              << "\"address\":" << string_json(inbox.address) << ","
              << "\"port\":" << inbox.port << ","
              << "\"interface\":" << string_json(inbox.interface_name) << ","
              << "\"protocol\":" << string_json(inbox.protocol) << ","
              << "\"token_required\":" << bool_json(inbox.token_required) << ","
              << "\"txt_records\":" << string_array_json(inbox.txt_records)
              << "}";
  }
  std::cout << "]\n";
}

void print_backends_json(const BackendRegistry& backends, const hyprshare::model::DoctorReport& report) {
  std::cout << "[";
  for (std::size_t index = 0; index < backends.all().size(); ++index) {
    const auto& backend = backends.all()[index];
    if (index > 0) {
      std::cout << ",";
    }

    std::vector<std::string> unmet;
    for (const auto& prereq : backend.prerequisites) {
      const auto it = report.facts.find(prereq);
      if (it == report.facts.end() || !it->second) {
        unmet.push_back(prereq);
      }
    }

    std::cout << "{"
              << "\"id\":" << string_json(backend.id) << ","
              << "\"capability\":" << string_json(hyprshare::model::to_string(backend.capability)) << ","
              << "\"support_tier\":" << string_json(hyprshare::model::to_string(backend.support_tier)) << ","
              << "\"summary\":" << string_json(backend.summary) << ","
              << "\"ready\":" << bool_json(unmet.empty()) << ","
              << "\"missing\":" << string_array_json(unmet)
              << "}";
  }
  std::cout << "]\n";
}

void print_profile_json(const AppProfile& profile) {
  std::cout << "{"
            << "\"id\":" << string_json(profile.id) << ","
            << "\"display_name\":" << string_json(profile.display_name) << ","
            << "\"matches\":" << string_array_json(profile.matches) << ","
            << "\"launch_commands\":" << string_array_json(profile.launch_commands) << ","
            << "\"launch_env\":" << string_array_json(profile.launch_env) << ","
            << "\"notes\":" << string_array_json(profile.notes) << ","
            << "\"screen_strategies\":[";
  for (std::size_t index = 0; index < profile.screen_strategies.size(); ++index) {
    const auto& strategy = profile.screen_strategies[index];
    if (index > 0) {
      std::cout << ",";
    }
    std::cout << "{"
              << "\"backend\":" << string_json(strategy.backend_id) << ","
              << "\"when\":" << string_array_json(strategy.conditions) << ","
              << "\"expose_as\":" << string_json(strategy.expose_as)
              << "}";
  }
  std::cout << "]}\n";
}

void print_doctor_json(const hyprshare::model::DoctorReport& report) {
  std::cout << "{"
            << "\"facts\":{";
  bool first = true;
  for (const auto& [fact, value] : report.facts) {
    if (!first) {
      std::cout << ",";
    }
    first = false;
    std::cout << string_json(fact) << ":" << bool_json(value);
  }
  std::cout << "},\"checks\":[";
  for (std::size_t index = 0; index < report.checks.size(); ++index) {
    const auto& check = report.checks[index];
    if (index > 0) {
      std::cout << ",";
    }
    std::cout << "{"
              << "\"id\":" << string_json(check.id) << ","
              << "\"status\":" << string_json(hyprshare::model::to_string(check.status)) << ","
              << "\"severity\":" << string_json(hyprshare::model::to_string(check.severity)) << ","
              << "\"summary\":" << string_json(check.summary) << ","
              << "\"remediation\":" << string_array_json(check.remediation)
              << "}";
  }
  std::cout << "]}\n";
}

void print_config_json(const Config& config, const Paths& paths) {
  std::cout << "{"
            << "\"config_file\":" << string_json(paths.config_file.string()) << ","
            << "\"general\":{"
            << "\"default_scope\":" << string_json(config.general.default_scope) << ","
            << "\"discoverability\":" << string_json(config.general.discoverability) << ","
            << "\"log_level\":" << string_json(config.general.log_level)
            << "},"
            << "\"screen\":{"
            << "\"preferred_backends\":" << string_array_json(config.screen.preferred_backends) << ","
            << "\"allow_fallback_to_obs\":" << bool_json(config.screen.allow_fallback_to_obs)
            << "},"
            << "\"files\":{"
            << "\"receive_dir\":" << string_json(config.files.receive_dir) << ","
            << "\"folder_shares_read_only_by_default\":" << bool_json(config.files.folder_shares_read_only_by_default)
            << "},"
            << "\"security\":{"
            << "\"require_explicit_clipboard_approval\":" << bool_json(config.security.require_explicit_clipboard_approval) << ","
            << "\"default_session_ttl_minutes\":" << config.security.default_session_ttl_minutes
            << "}}\n";
}

void print_clipboard_text_json(const ClipboardText& clipboard) {
  std::cout << "{"
            << "\"capability\":\"clipboard\","
            << "\"backend\":" << string_json(clipboard.backend) << ","
            << "\"byte_count\":" << clipboard.byte_count << ","
            << "\"captured_at_unix\":" << clipboard.captured_at_unix << ","
            << "\"text\":" << string_json(clipboard.text)
            << "}\n";
}

void print_inbox_status_json(
  const Config& config,
  const std::filesystem::path& resolved_receive_dir) {
  std::error_code error;
  const bool exists = std::filesystem::exists(resolved_receive_dir, error);
  const bool is_directory = exists && std::filesystem::is_directory(resolved_receive_dir, error);
  const bool writable = exists && is_directory && path_has_write_bit(resolved_receive_dir);
  std::cout << "{"
            << "\"configured_receive_dir\":" << string_json(config.files.receive_dir) << ","
            << "\"resolved_receive_dir\":" << string_json(resolved_receive_dir.string()) << ","
            << "\"exists\":" << bool_json(exists) << ","
            << "\"is_directory\":" << bool_json(is_directory) << ","
            << "\"writable\":" << bool_json(writable) << ","
            << "\"default_host\":\"0.0.0.0\","
            << "\"default_port\":28556,"
            << "\"advertised_hosts\":" << string_array_json(inbox_advertised_hosts("0.0.0.0"))
            << "}\n";
}

void print_inbox_open_json(
  const std::filesystem::path& receive_dir,
  const std::string_view command,
  const bool dry_run_mode) {
  std::cout << "{"
            << "\"receive_dir\":" << string_json(receive_dir.string()) << ","
            << "\"command\":" << string_json(command) << ","
            << "\"dry_run\":" << bool_json(dry_run_mode) << ","
            << "\"opened\":" << bool_json(!dry_run_mode)
            << "}\n";
}

void print_inbox_announce_json(
  const InboxAnnouncePlan& plan,
  const std::optional<int> pid,
  const bool dry_run_mode) {
  std::cout << "{"
            << "\"name\":" << string_json(plan.name) << ","
            << "\"service_type\":" << string_json(plan.service_type) << ","
            << "\"port\":" << plan.port << ","
            << "\"token_required\":" << bool_json(plan.token_required) << ","
            << "\"txt_records\":" << string_array_json(plan.txt_records) << ","
            << "\"command\":" << string_json(plan.command) << ","
            << "\"dry_run\":" << bool_json(dry_run_mode) << ","
            << "\"started\":" << bool_json(pid.has_value()) << ","
            << "\"pid\":" << pid.value_or(0)
            << "}\n";
}

void print_inbox_history_json(const std::vector<hyprshare::model::InboxHistoryEntry>& entries) {
  std::cout << "[";
  for (std::size_t index = 0; index < entries.size(); ++index) {
    if (index > 0) {
      std::cout << ",";
    }
    const auto& entry = entries[index];
    std::cout << "{"
              << "\"entry_id\":" << string_json(entry.entry_id) << ","
              << "\"file_name\":" << string_json(entry.file_name) << ","
              << "\"destination_path\":" << string_json(entry.destination_path) << ","
              << "\"bytes_received\":" << entry.bytes_received << ","
              << "\"sha256\":" << string_json(entry.sha256) << ","
              << "\"received_at_unix\":" << entry.received_at_unix
              << "}";
  }
  std::cout << "]\n";
}

void print_cleared_inbox_history_json(const std::size_t removed_count) {
  std::cout << "{"
            << "\"removed_count\":" << removed_count
            << "}\n";
}

void print_inbox_received_json(const hyprshare::model::InboxReceivedFile& received) {
  std::cout << inbox_received_json_fragment(received) << "\n";
}

void print_inbox_received_list_json(const std::vector<hyprshare::model::InboxReceivedFile>& received_files) {
  std::cout << "[";
  for (std::size_t index = 0; index < received_files.size(); ++index) {
    if (index > 0) {
      std::cout << ",";
    }
    const auto& received = received_files[index];
    std::cout << inbox_received_json_fragment(received);
  }
  std::cout << "]\n";
}

void print_inbox_put_json(
  const std::string_view host,
  const std::uint16_t port,
  const std::filesystem::path& file_path,
  const InboxUploadResult& upload) {
  std::cout << "{"
            << "\"host\":" << string_json(host) << ","
            << "\"port\":" << port << ","
            << "\"file_path\":" << string_json(file_path.string()) << ","
            << "\"uploaded_path\":" << string_json(upload.uploaded_path.string()) << ","
            << "\"remote_name\":" << string_json(upload.remote_name) << ","
            << "\"archived_directory\":" << bool_json(upload.archived_directory) << ","
            << "\"bytes_sent\":" << upload.bytes_sent << ","
            << "\"sha256\":" << string_json(upload.sha256)
            << "}\n";
}

void print_inbox_put_batch_json(
  const std::string_view host,
  const std::uint16_t port,
  const std::vector<std::filesystem::path>& file_paths,
  const std::vector<InboxUploadResult>& uploads) {
  std::cout << "{"
            << "\"host\":" << string_json(host) << ","
            << "\"port\":" << port << ","
            << "\"upload_count\":" << uploads.size() << ","
            << "\"uploads\":[";
  for (std::size_t index = 0; index < uploads.size(); ++index) {
    if (index > 0) {
      std::cout << ",";
    }
    std::cout << inbox_put_upload_json_fragment(file_paths[index], uploads[index]);
  }
  std::cout << "]}\n";
}

void print_inbox_extract_json(const InboxExtractResult& result) {
  std::cout << "{"
            << "\"archive_path\":" << string_json(result.inspection.archive_path.string()) << ","
            << "\"destination_dir\":" << string_json(result.destination_dir.string()) << ","
            << "\"extracted_path\":" << string_json(result.extracted_path.string()) << ","
            << "\"top_level\":" << string_json(result.inspection.top_level) << ","
            << "\"entry_count\":" << result.inspection.entries.size() << ","
            << "\"entries\":" << string_array_json(result.inspection.entries) << ","
            << "\"dry_run\":" << bool_json(result.dry_run) << ","
            << "\"extracted\":" << bool_json(!result.dry_run)
            << "}\n";
}

void print_inbox_received_with_extract_json(
  const std::vector<hyprshare::model::InboxReceivedFile>& received_files,
  const std::vector<InboxExtractResult>& extracted_archives) {
  std::cout << "{"
            << "\"received\":[";
  for (std::size_t index = 0; index < received_files.size(); ++index) {
    if (index > 0) {
      std::cout << ",";
    }
    std::cout << inbox_received_json_fragment(received_files[index]);
  }
  std::cout << "],\"extracted\":[";
  for (std::size_t index = 0; index < extracted_archives.size(); ++index) {
    if (index > 0) {
      std::cout << ",";
    }
    std::cout << inbox_extract_json_fragment(extracted_archives[index]);
  }
  std::cout << "]}\n";
}

void print_device_identity_json(const DeviceIdentity& identity) {
  std::cout << "{"
            << "\"device_id\":" << string_json(identity.device_id) << ","
            << "\"device_label\":" << string_json(identity.device_label) << ","
            << "\"created_at_unix\":" << identity.created_at_unix
            << "}\n";
}

void print_trusted_peers_json(const std::vector<TrustedPeer>& peers) {
  std::cout << "[";
  for (std::size_t index = 0; index < peers.size(); ++index) {
    if (index > 0) {
      std::cout << ",";
    }
    const auto& peer = peers[index];
    std::cout << "{"
              << "\"device_id\":" << string_json(peer.device_id) << ","
              << "\"device_label\":" << string_json(peer.device_label) << ","
              << "\"added_at_unix\":" << peer.added_at_unix
              << "}";
  }
  std::cout << "]\n";
}

void print_trusted_peer_json(const TrustedPeer& peer) {
  std::cout << "{"
            << "\"device_id\":" << string_json(peer.device_id) << ","
            << "\"device_label\":" << string_json(peer.device_label) << ","
            << "\"added_at_unix\":" << peer.added_at_unix
            << "}\n";
}

void print_exported_peer_json(const DeviceIdentity& identity, const std::filesystem::path& export_path) {
  std::cout << "{"
            << "\"device_id\":" << string_json(identity.device_id) << ","
            << "\"device_label\":" << string_json(identity.device_label) << ","
            << "\"export_path\":" << string_json(export_path.string())
            << "}\n";
}

void print_peer_invite_json(const PeerInvite& invite) {
  std::cout << "{"
            << "\"invite_kind\":" << string_json(hyprshare::model::to_string(invite.kind)) << ","
            << "\"from_device_id\":" << string_json(invite.from_device_id) << ","
            << "\"from_device_label\":" << string_json(invite.from_device_label) << ","
            << "\"target_device_id\":" << string_json(invite.target_device_id) << ","
            << "\"created_at_unix\":" << invite.created_at_unix
            << "}\n";
}

void print_written_peer_invite_json(const PeerInvite& invite, const std::filesystem::path& output_path) {
  std::cout << "{"
            << "\"invite_kind\":" << string_json(hyprshare::model::to_string(invite.kind)) << ","
            << "\"from_device_id\":" << string_json(invite.from_device_id) << ","
            << "\"from_device_label\":" << string_json(invite.from_device_label) << ","
            << "\"target_device_id\":" << string_json(invite.target_device_id) << ","
            << "\"created_at_unix\":" << invite.created_at_unix << ","
            << "\"invite_path\":" << string_json(output_path.string())
            << "}\n";
}

void print_removed_peer_json(const std::string_view device_id, const std::filesystem::path& peer_path) {
  std::cout << "{"
            << "\"device_id\":" << string_json(device_id) << ","
            << "\"peer_path\":" << string_json(peer_path.string()) << ","
            << "\"removed\":" << bool_json(true)
            << "}\n";
}

void print_plan_json(const SharePlan& plan, const bool dry_run) {
  std::cout << "{"
            << "\"capability\":" << string_json(hyprshare::model::to_string(plan.capability)) << ","
            << "\"app_hint\":" << string_json(plan.app_hint) << ","
            << "\"selected_backend\":" << string_json(plan.selected_backend_id) << ","
            << "\"dry_run\":" << bool_json(dry_run) << ","
            << "\"notes\":" << string_array_json(plan.notes) << ","
            << "\"candidates\":[";
  for (std::size_t index = 0; index < plan.candidates.size(); ++index) {
    const auto& candidate = plan.candidates[index];
    if (index > 0) {
      std::cout << ",";
    }
    std::cout << "{"
              << "\"backend\":" << string_json(candidate.backend.id) << ","
              << "\"summary\":" << string_json(candidate.backend.summary) << ","
              << "\"support_tier\":" << string_json(hyprshare::model::to_string(candidate.backend.support_tier)) << ","
              << "\"score\":" << candidate.score << ","
              << "\"viable\":" << bool_json(candidate.viable) << ","
              << "\"missing\":" << string_array_json(candidate.unmet_conditions)
              << "}";
  }
  std::cout << "]}\n";
}

void print_launch_plan_json(const AppLaunchPlan& plan, const bool dry_run) {
  std::cout << "{"
            << "\"app_hint\":" << string_json(plan.app_hint) << ","
            << "\"profile_id\":" << string_json(plan.profile_id) << ","
            << "\"display_name\":" << string_json(plan.display_name) << ","
            << "\"resolved_command\":" << string_json(plan.resolved_command) << ","
            << "\"selected_backend\":" << string_json(plan.selected_backend_id) << ","
            << "\"ready\":" << bool_json(plan.ready) << ","
            << "\"dry_run\":" << bool_json(dry_run) << ","
            << "\"env_overrides\":" << string_array_json(plan.env_overrides) << ","
            << "\"notes\":" << string_array_json(plan.notes) << ","
            << "\"actions\":[";
  for (std::size_t index = 0; index < plan.actions.size(); ++index) {
    const auto& action = plan.actions[index];
    if (index > 0) {
      std::cout << ",";
    }
    std::cout << "{"
              << "\"kind\":" << string_json(hyprshare::model::to_string(action.kind)) << ","
              << "\"description\":" << string_json(action.description) << ","
              << "\"command\":" << string_json(action.command)
              << "}";
  }
  std::cout << "]}\n";
}

void print_file_manifest_json(const FileShareManifest& manifest) {
  std::cout << file_manifest_json_string(manifest) << "\n";
}

void print_staged_share_json(const StagedFileShare& staged) {
  std::cout << "{"
            << "\"session_id\":" << string_json(staged.session_id) << ","
            << "\"stage_path\":" << string_json(staged.stage_path) << ","
            << "\"manifest_path\":" << string_json(staged.manifest_path) << ","
            << "\"metadata_path\":" << string_json(staged.metadata_path) << ","
            << "\"payload_path\":" << string_json(staged.payload_path) << ","
            << "\"checksum_path\":" << string_json(staged.checksum_path) << ","
            << "\"copied_file_count\":" << staged.copied_file_count << ","
            << "\"copied_bytes\":" << staged.copied_bytes << ","
            << "\"manifest\":" << file_manifest_json_string(staged.manifest)
            << "}\n";
}

void print_session_list_json(const std::vector<StagedFileShare>& sessions) {
  std::cout << "[";
  for (std::size_t index = 0; index < sessions.size(); ++index) {
    const auto& session = sessions[index];
    if (index > 0) {
      std::cout << ",";
    }
    std::cout << "{"
              << "\"session_id\":" << string_json(session.session_id) << ","
              << "\"capability\":" << string_json(hyprshare::model::to_string(session.manifest.capability)) << ","
              << "\"share_name\":" << string_json(session.manifest.share_name) << ","
              << "\"file_count\":" << session.manifest.file_count << ","
              << "\"total_bytes\":" << session.manifest.total_bytes
              << "}";
  }
  std::cout << "]\n";
}

void print_deleted_session_json(const std::string_view session_id, const std::filesystem::path& stage_path) {
  std::cout << "{"
            << "\"session_id\":" << string_json(session_id) << ","
            << "\"stage_path\":" << string_json(stage_path.string()) << ","
            << "\"deleted\":" << bool_json(true)
            << "}\n";
}

void print_pruned_sessions_json(const std::vector<std::filesystem::path>& pruned_sessions) {
  std::cout << "{"
            << "\"pruned_count\":" << pruned_sessions.size() << ","
            << "\"sessions\":[";
  for (std::size_t index = 0; index < pruned_sessions.size(); ++index) {
    if (index > 0) {
      std::cout << ",";
    }
    const auto& stage_path = pruned_sessions[index];
    std::cout << "{"
              << "\"session_id\":" << string_json(stage_path.filename().string()) << ","
              << "\"stage_path\":" << string_json(stage_path.string())
              << "}";
  }
  std::cout << "]}\n";
}

void print_exported_share_json(
  const std::string_view session_id,
  const std::filesystem::path& stage_path,
  const std::filesystem::path& bundle_path) {
  std::cout << "{"
            << "\"session_id\":" << string_json(session_id) << ","
            << "\"stage_path\":" << string_json(stage_path.string()) << ","
            << "\"bundle_path\":" << string_json(bundle_path.string())
            << "}\n";
}

void print_remote_offer_json(const RemoteFileShareOffer& offer) {
  std::cout << "{"
            << "\"session_id\":" << string_json(offer.session_id) << ","
            << "\"host\":" << string_json(offer.host) << ","
            << "\"port\":" << offer.port << ","
            << "\"bundle_size\":" << offer.bundle_size << ","
            << "\"sender_device_id\":" << string_json(offer.sender_device_id) << ","
            << "\"sender_device_label\":" << string_json(offer.sender_device_label) << ","
            << "\"allowed_peer_id\":" << string_json(offer.allowed_peer_id) << ","
            << "\"auth_required\":" << bool_json(offer.auth_required) << ","
            << "\"auth_expires_at_unix\":" << offer.auth_expires_at_unix << ","
            << "\"manifest\":" << file_manifest_json_string(offer.manifest)
            << "}\n";
}

void print_probe_json(
  const std::string_view host,
  const std::uint16_t port,
  const bool reachable,
  const std::string_view error) {
  std::cout << "{"
            << "\"host\":" << string_json(host) << ","
            << "\"port\":" << port << ","
            << "\"reachable\":" << bool_json(reachable) << ","
            << "\"error\":" << string_json(error)
            << "}\n";
}

void print_served_share_json(
  const std::string_view session_id,
  const std::string_view host,
  const std::uint16_t port,
  const std::uintmax_t bytes_sent) {
  std::cout << "{"
            << "\"session_id\":" << string_json(session_id) << ","
            << "\"host\":" << string_json(host) << ","
            << "\"port\":" << port << ","
            << "\"bytes_sent\":" << bytes_sent
            << "}\n";
}

void print_verified_share_json(const StagedFileShare& staged) {
  std::cout << "{"
            << "\"session_id\":" << string_json(staged.session_id) << ","
            << "\"payload_path\":" << string_json(staged.payload_path) << ","
            << "\"checksum_path\":" << string_json(staged.checksum_path) << ","
            << "\"verified\":" << bool_json(true)
            << "}\n";
}

void print_received_share_json(const ReceivedFileShare& received) {
  std::cout << "{"
            << "\"session_id\":" << string_json(received.session_id) << ","
            << "\"destination_path\":" << string_json(received.destination_path) << ","
            << "\"copied_file_count\":" << received.copied_file_count << ","
            << "\"copied_bytes\":" << received.copied_bytes << ","
            << "\"manifest\":" << file_manifest_json_string(received.manifest)
            << "}\n";
}

void print_support_bundle_json(const SupportBundle& bundle) {
  std::vector<std::string> files;
  files.reserve(bundle.files.size());
  for (const auto& file : bundle.files) {
    files.push_back(file.string());
  }
  std::cout << "{"
            << "\"directory\":" << string_json(bundle.directory.string()) << ","
            << "\"files\":" << string_array_json(files)
            << "}\n";
}

}  // namespace hyprshare::cli
