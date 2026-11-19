#include "output_text.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <arpa/inet.h>
#include <cstdlib>
#include <filesystem>
#include <ifaddrs.h>
#include <iostream>
#include <net/if.h>
#include <netinet/in.h>
#include <optional>
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

namespace {

// The interface that carries the default route. Its address is the one other
// devices on the same network can reach, so it is listed first.
std::string default_route_interface() {
  std::ifstream route("/proc/net/route");
  if (!route.is_open()) {
    return "";
  }

  std::string line;
  if (!std::getline(route, line)) {
    return "";
  }

  while (std::getline(route, line)) {
    std::istringstream stream(line);
    std::string interface_name;
    std::string destination;
    stream >> interface_name >> destination;
    if (destination == "00000000") {
      return interface_name;
    }
  }

  return "";
}

bool is_link_local(const std::string& address) {
  return address.rfind("169.254.", 0) == 0;
}

std::vector<std::string> local_ipv4_addresses() {
  ifaddrs* interfaces = nullptr;
  if (getifaddrs(&interfaces) != 0) {
    return {};
  }

  const std::string primary_interface = default_route_interface();

  std::vector<std::string> primary;
  std::vector<std::string> others;
  for (const ifaddrs* current = interfaces; current != nullptr; current = current->ifa_next) {
    if (current->ifa_addr == nullptr || current->ifa_addr->sa_family != AF_INET) {
      continue;
    }
    if ((current->ifa_flags & IFF_UP) == 0 || (current->ifa_flags & IFF_LOOPBACK) != 0) {
      continue;
    }

    const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(current->ifa_addr);
    char buffer[INET_ADDRSTRLEN]{};
    if (inet_ntop(AF_INET, &ipv4->sin_addr, buffer, sizeof(buffer)) == nullptr) {
      continue;
    }

    const std::string address(buffer);
    if (address.rfind("127.", 0) == 0 || is_link_local(address)) {
      continue;
    }

    auto& target = (!primary_interface.empty() && current->ifa_name != nullptr &&
                     primary_interface == current->ifa_name)
      ? primary
      : others;
    if (std::find(target.begin(), target.end(), address) == target.end()) {
      target.push_back(address);
    }
  }

  freeifaddrs(interfaces);
  std::sort(primary.begin(), primary.end());
  std::sort(others.begin(), others.end());

  std::vector<std::string> addresses = primary;
  // A machine with many virtual interfaces would otherwise drown the useful
  // address in a wall of container bridges.
  constexpr std::size_t kMaxOtherAddresses = 3;
  for (const auto& address : others) {
    if (addresses.size() >= primary.size() + kMaxOtherAddresses) {
      break;
    }
    addresses.push_back(address);
  }
  return addresses;
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

bool path_has_write_bit(const std::filesystem::path& path) {
  std::error_code error;
  const auto status = std::filesystem::status(path, error);
  if (error) {
    return false;
  }
  const auto permissions = status.permissions();
  return (permissions & std::filesystem::perms::owner_write) != std::filesystem::perms::none ||
         (permissions & std::filesystem::perms::group_write) != std::filesystem::perms::none ||
         (permissions & std::filesystem::perms::others_write) != std::filesystem::perms::none;
}

std::vector<std::string> inbox_advertised_hosts(const std::string_view bind_host) {
  if (bind_host == "0.0.0.0") {
    auto addresses = local_ipv4_addresses();
    if (!addresses.empty()) {
      return addresses;
    }
    return {"<hyprland-ip>"};
  }
  return {std::string(bind_host)};
}

void print_status_text(const Paths& paths, const hyprshare::model::DoctorReport& report, const BackendRegistry& backends, const AdapterRegistry& adapters) {
  std::cout << "hyprshare " << HYPRSHARE_VERSION << "\n";
  std::cout << "Data directory:  " << paths.data_dir << "\n";
  std::cout << "Config file:     " << paths.config_file << "\n";
  std::cout << "State directory: " << paths.state_dir << "\n\n";
  std::cout << "Catalogs\n";
  std::cout << "  Backends: " << backends.all().size() << "\n";
  std::cout << "  Adapters: " << adapters.all().size() << "\n\n";
  std::cout << "Facts\n";
  for (const auto& [fact, value] : report.facts) {
    std::cout << "  " << fact << ": " << (value ? "yes" : "no") << "\n";
  }
}

void print_discovery_status_text(const DiscoveryStatus& status) {
  std::cout << "LAN discovery\n";
  std::cout << "  avahi-publish-service: " << (status.avahi_publish_service_available ? "available" : "missing") << "\n";
  std::cout << "  avahi-browse:          " << (status.avahi_browse_available ? "available" : "missing") << "\n";
  std::cout << "  avahi-daemon:          " << (status.avahi_daemon_active ? "active" : "inactive") << "\n";
  std::cout << "  Announce ready:        " << (status.ready_to_announce ? "yes" : "no") << "\n";
  std::cout << "  Browse ready:          " << (status.ready_to_browse ? "yes" : "no") << "\n";
  if (!status.ready_to_announce) {
    std::cout << "\nTo announce an inbox receiver, install Avahi tools and start avahi-daemon.service.\n";
  }
}

void print_discovered_inboxes_text(const std::vector<DiscoveredInbox>& inboxes) {
  if (inboxes.empty()) {
    std::cout << "No hyprshare inbox receivers discovered.\n";
    return;
  }

  for (const auto& inbox : inboxes) {
    std::cout << inbox.name << " " << inbox.address << ":" << inbox.port
              << " token=" << (inbox.token_required ? "required" : "none")
              << " host=" << inbox.host << "\n";
  }
}

void print_backends_text(const BackendRegistry& backends, const hyprshare::model::DoctorReport& report) {
  for (const auto& backend : backends.all()) {
    std::vector<std::string> unmet;
    for (const auto& prereq : backend.prerequisites) {
      const auto it = report.facts.find(prereq);
      if (it == report.facts.end() || !it->second) {
        unmet.push_back(prereq);
      }
    }
    std::cout << backend.id << " [" << hyprshare::model::to_string(backend.capability) << "] "
              << hyprshare::model::to_string(backend.support_tier) << "\n";
    std::cout << "  " << backend.summary << "\n";
    if (unmet.empty()) {
      std::cout << "  Status: ready\n";
    } else {
      std::cout << "  Missing: " << hyprshare::util::join(unmet, ", ") << "\n";
    }
  }
}

void print_profile_text(const AppProfile& profile) {
  std::cout << profile.display_name << " (" << profile.id << ")\n";
  std::cout << "Matches: " << hyprshare::util::join(profile.matches, ", ") << "\n";
  if (!profile.launch_commands.empty()) {
    std::cout << "Launch commands: " << hyprshare::util::join(profile.launch_commands, ", ") << "\n";
  }
  if (!profile.launch_env.empty()) {
    std::cout << "Launch env: " << hyprshare::util::join(profile.launch_env, ", ") << "\n";
  }
  if (!profile.notes.empty()) {
    std::cout << "Notes:\n";
    for (const auto& note : profile.notes) {
      std::cout << "  - " << note << "\n";
    }
  }
  std::cout << "Screen strategies:\n";
  for (const auto& strategy : profile.screen_strategies) {
    std::cout << "  - " << strategy.backend_id;
    if (!strategy.conditions.empty()) {
      std::cout << " when " << hyprshare::util::join(strategy.conditions, ", ");
    }
    if (!strategy.expose_as.empty()) {
      std::cout << " expose_as=" << strategy.expose_as;
    }
    std::cout << "\n";
  }
}

void print_doctor_text(const hyprshare::model::DoctorReport& report) {
  for (const auto& check : report.checks) {
    std::cout << check.id << " [" << hyprshare::model::to_string(check.status) << "] "
              << check.summary << "\n";
    for (const auto& remediation : check.remediation) {
      std::cout << "  fix: " << remediation << "\n";
    }
  }
}

void print_config_text(const Config& config, const Paths& paths) {
  std::cout << "Config file: " << paths.config_file << "\n\n";
  std::cout << "[general]\n";
  std::cout << "default_scope = " << config.general.default_scope << "\n";
  std::cout << "discoverability = " << config.general.discoverability << "\n";
  std::cout << "log_level = " << config.general.log_level << "\n\n";
  std::cout << "[screen]\n";
  std::cout << "preferred_backends = " << hyprshare::util::join(config.screen.preferred_backends, ", ") << "\n";
  std::cout << "allow_fallback_to_obs = " << (config.screen.allow_fallback_to_obs ? "true" : "false") << "\n\n";
  std::cout << "[files]\n";
  std::cout << "receive_dir = " << config.files.receive_dir << "\n";
  std::cout << "folder_shares_read_only_by_default = "
            << (config.files.folder_shares_read_only_by_default ? "true" : "false") << "\n\n";
  std::cout << "[security]\n";
  std::cout << "require_explicit_clipboard_approval = "
            << (config.security.require_explicit_clipboard_approval ? "true" : "false") << "\n";
  std::cout << "default_session_ttl_minutes = " << config.security.default_session_ttl_minutes << "\n";
}

void print_clipboard_summary_text(const ClipboardText& clipboard) {
  std::cout << "Capability: clipboard\n";
  std::cout << "Backend:    " << clipboard.backend << "\n";
  std::cout << "Bytes:      " << clipboard.byte_count << "\n";
  std::cout << "Captured:   " << clipboard.captured_at_unix << "\n";
}

void print_clipboard_write_text(const ClipboardText& clipboard) {
  std::cout << "Clipboard updated through " << clipboard.backend << ".\n";
  std::cout << "Bytes: " << clipboard.byte_count << "\n";
}

void print_inbox_status_text(
  const Config& config,
  const std::filesystem::path& resolved_receive_dir) {
  std::error_code error;
  const bool exists = std::filesystem::exists(resolved_receive_dir, error);
  const bool is_directory = exists && std::filesystem::is_directory(resolved_receive_dir, error);
  const bool writable = exists && is_directory && path_has_write_bit(resolved_receive_dir);

  std::cout << "Inbox receive directory\n";
  std::cout << "  Configured: " << config.files.receive_dir << "\n";
  std::cout << "  Resolved:   " << resolved_receive_dir.string() << "\n";
  std::cout << "  Exists:     " << (exists ? "yes" : "no") << "\n";
  std::cout << "  Directory:  " << (is_directory ? "yes" : "no") << "\n";
  std::cout << "  Writable:   " << (writable ? "yes" : "no") << "\n";
  if (!exists) {
    std::cout << "  Note:       `inbox serve` and `inbox open` will create it when needed.\n";
  }

  std::cout << "\nDefault receiver\n";
  std::cout << "  Listen:     0.0.0.0:28556\n";
  const auto hosts = inbox_advertised_hosts("0.0.0.0");
  std::cout << "  Endpoints:  " << hyprshare::util::join(hosts, ", ") << "\n";
  std::cout << "\nUseful commands\n";
  std::cout << "  hyprshare inbox receive --timeout 120\n";
  std::cout << "  hyprshare inbox serve --host 0.0.0.0 --port 28556 --to "
            << hyprshare::util::shell_quote(resolved_receive_dir.string()) << " --timeout 120\n";
  std::cout << "  hyprshare inbox open\n";
}

void print_inbox_open_text(
  const std::filesystem::path& receive_dir,
  const std::string_view command,
  const bool dry_run_mode) {
  std::cout << "Inbox directory: " << receive_dir.string() << "\n";
  std::cout << "Command: " << command << "\n";
  std::cout << "Opened: " << (dry_run_mode ? "no (dry-run)" : "yes") << "\n";
}

void print_inbox_announce_text(
  const InboxAnnouncePlan& plan,
  const std::optional<int> pid,
  const bool dry_run_mode) {
  std::cout << "Inbox announcement\n";
  std::cout << "  Name:      " << plan.name << "\n";
  std::cout << "  Type:      " << plan.service_type << "\n";
  std::cout << "  Port:      " << plan.port << "\n";
  std::cout << "  Token:     " << (plan.token_required ? "required" : "not required") << "\n";
  std::cout << "  Command:   " << plan.command << "\n";
  if (pid.has_value()) {
    std::cout << "  PID:       " << *pid << "\n";
    std::cout << "  Stop:      kill " << *pid << "\n";
  } else {
    std::cout << "  Started:   " << (dry_run_mode ? "no (dry-run)" : "no") << "\n";
  }
  std::cout << "\nRun the receiver on the same port:\n";
  std::cout << "  hyprshare inbox serve --host 0.0.0.0 --port " << plan.port;
  if (plan.token_required) {
    std::cout << " --token <token>";
  }
  std::cout << "\n";
}

void print_inbox_history_text(const std::vector<hyprshare::model::InboxHistoryEntry>& entries) {
  if (entries.empty()) {
    std::cout << "No inbox history.\n";
    return;
  }

  for (const auto& entry : entries) {
    std::cout << entry.received_at_unix << " " << entry.file_name << " bytes=" << entry.bytes_received
              << " path=" << entry.destination_path;
    if (!entry.sha256.empty()) {
      std::cout << " sha256=" << entry.sha256;
    }
    std::cout << "\n";
  }
}

void print_cleared_inbox_history_text(const std::size_t removed_count) {
  std::cout << "Removed inbox history entries: " << removed_count << "\n";
}

void print_inbox_received_text(const hyprshare::model::InboxReceivedFile& received) {
  std::cout << "Received: " << received.file_name << "\n";
  std::cout << "Path:     " << received.destination_path << "\n";
  std::cout << "Bytes:    " << received.bytes_received << "\n";
  if (!received.sha256.empty()) {
    std::cout << "SHA-256:  " << received.sha256 << "\n";
  }
}

void print_inbox_received_list_text(const std::vector<hyprshare::model::InboxReceivedFile>& received_files) {
  for (const auto& received : received_files) {
    print_inbox_received_text(received);
  }
}

void print_inbox_put_text(
  const std::string_view host,
  const std::uint16_t port,
  const std::filesystem::path& file_path,
  const InboxUploadResult& upload) {
  std::cout << "Uploaded: " << file_path << "\n";
  std::cout << "Endpoint: " << host << ":" << port << "\n";
  std::cout << "Remote:   " << upload.remote_name << "\n";
  std::cout << "Mode:     " << (upload.archived_directory ? "directory archive" : "file") << "\n";
  std::cout << "Bytes:    " << upload.bytes_sent << "\n";
  if (!upload.sha256.empty()) {
    std::cout << "SHA-256:  " << upload.sha256 << "\n";
  }
}

void print_inbox_put_batch_text(
  const std::string_view host,
  const std::uint16_t port,
  const std::vector<std::filesystem::path>& file_paths,
  const std::vector<InboxUploadResult>& uploads) {
  for (std::size_t index = 0; index < uploads.size(); ++index) {
    if (index > 0) {
      std::cout << "\n";
    }
    print_inbox_put_text(host, port, file_paths[index], uploads[index]);
  }
}

void print_inbox_extract_text(const InboxExtractResult& result) {
  std::cout << "Archive:     " << result.inspection.archive_path.string() << "\n";
  std::cout << "Destination: " << result.destination_dir.string() << "\n";
  std::cout << "Top-level:   " << result.inspection.top_level << "\n";
  std::cout << "Entries:     " << result.inspection.entries.size() << "\n";
  std::cout << "Extracted:   " << (result.dry_run ? "no (dry-run)" : "yes") << "\n";
  std::cout << "Path:        " << result.extracted_path.string() << "\n";
}

void print_inbox_auto_extract_text(const std::vector<InboxExtractResult>& extracted_archives) {
  if (extracted_archives.empty()) {
    return;
  }

  std::cout << "\nExtracted archives:\n";
  for (const auto& extracted : extracted_archives) {
    std::cout << "  " << extracted.inspection.archive_path.filename().string() << " -> "
              << extracted.extracted_path.string() << "\n";
  }
}

void print_device_identity_text(const DeviceIdentity& identity) {
  std::cout << "Device ID:    " << identity.device_id << "\n";
  std::cout << "Device label: " << identity.device_label << "\n";
  std::cout << "Created at:   " << identity.created_at_unix << "\n";
}

void print_trusted_peers_text(const std::vector<TrustedPeer>& peers) {
  if (peers.empty()) {
    std::cout << "No trusted peers.\n";
    return;
  }

  for (const auto& peer : peers) {
    std::cout << peer.device_id << " " << peer.device_label << " added_at=" << peer.added_at_unix << "\n";
  }
}

void print_trusted_peer_text(const TrustedPeer& peer) {
  std::cout << "Device ID:    " << peer.device_id << "\n";
  std::cout << "Device label: " << peer.device_label << "\n";
  std::cout << "Added at:     " << peer.added_at_unix << "\n";
}

void print_exported_peer_text(const DeviceIdentity& identity, const std::filesystem::path& export_path) {
  std::cout << "Exported peer card: " << export_path << "\n";
  std::cout << "Device ID:          " << identity.device_id << "\n";
  std::cout << "Device label:       " << identity.device_label << "\n";
}

void print_peer_invite_text(const PeerInvite& invite) {
  std::cout << "Invite kind:   " << hyprshare::model::to_string(invite.kind) << "\n";
  std::cout << "From device:   " << invite.from_device_label << " (" << invite.from_device_id << ")\n";
  std::cout << "Target device: " << (invite.target_device_id.empty() ? "<none>" : invite.target_device_id) << "\n";
  std::cout << "Created at:    " << invite.created_at_unix << "\n";
}

void print_written_peer_invite_text(const PeerInvite& invite, const std::filesystem::path& output_path) {
  std::cout << "Invite path:   " << output_path << "\n";
  print_peer_invite_text(invite);
}

void print_removed_peer_text(const std::string_view device_id, const std::filesystem::path& peer_path) {
  std::cout << "Removed peer: " << device_id << "\n";
  std::cout << "Peer path:    " << peer_path << "\n";
}

void print_plan_text(const SharePlan& plan, const bool dry_run) {
  std::cout << "Capability: " << hyprshare::model::to_string(plan.capability) << "\n";
  std::cout << "App hint:   " << (plan.app_hint.empty() ? "<none>" : plan.app_hint) << "\n";
  std::cout << "Selected:   " << (plan.selected_backend_id.empty() ? "<none>" : plan.selected_backend_id) << "\n\n";
  std::cout << "Candidates:\n";
  for (const auto& candidate : plan.candidates) {
    std::cout << "  - " << candidate.backend.id << " score=" << candidate.score
              << " viable=" << (candidate.viable ? "yes" : "no") << "\n";
    if (!candidate.unmet_conditions.empty()) {
      std::cout << "    missing: " << hyprshare::util::join(candidate.unmet_conditions, ", ") << "\n";
    }
  }
  if (!plan.notes.empty()) {
    std::cout << "\nNotes:\n";
    for (const auto& note : plan.notes) {
      std::cout << "  - " << note << "\n";
    }
  }
  if (!dry_run) {
    std::cout << "\nExecution is not wired yet. This command currently produces the concrete backend plan only.\n";
  }
}

void print_launch_plan_text(const AppLaunchPlan& plan, const bool dry_run) {
  std::cout << "App hint:    " << (plan.app_hint.empty() ? "<none>" : plan.app_hint) << "\n";
  std::cout << "Profile:     " << (plan.profile_id.empty() ? "<none>" : plan.profile_id) << "\n";
  std::cout << "Executable:  " << (plan.resolved_command.empty() ? "<none>" : plan.resolved_command) << "\n";
  std::cout << "Backend:     " << (plan.selected_backend_id.empty() ? "<none>" : plan.selected_backend_id) << "\n";
  std::cout << "Ready:       " << (plan.ready ? "yes" : "no") << "\n\n";

  if (!plan.actions.empty()) {
    std::cout << "Actions:\n";
    for (const auto& action : plan.actions) {
      std::cout << "  - " << hyprshare::model::to_string(action.kind) << ": " << action.description << "\n";
      std::cout << "    command: " << action.command << "\n";
    }
  }

  if (!plan.notes.empty()) {
    std::cout << "\nNotes:\n";
    for (const auto& note : plan.notes) {
      std::cout << "  - " << note << "\n";
    }
  }

  if (!dry_run && !plan.ready) {
    std::cout << "\nLaunch execution is blocked because the plan is incomplete.\n";
  }
}

void print_file_manifest_text(const FileShareManifest& manifest) {
  std::cout << "Capability:  " << hyprshare::model::to_string(manifest.capability) << "\n";
  std::cout << "Name:        " << manifest.share_name << "\n";
  std::cout << "Root path:   " << manifest.root_path << "\n";
  std::cout << "Read-only:   " << (manifest.read_only ? "yes" : "no") << "\n";
  std::cout << "Files:       " << manifest.file_count << "\n";
  std::cout << "Directories: " << manifest.directory_count << "\n";
  std::cout << "Total bytes: " << manifest.total_bytes << "\n";

  if (!manifest.notes.empty()) {
    std::cout << "\nNotes:\n";
    for (const auto& note : manifest.notes) {
      std::cout << "  - " << note << "\n";
    }
  }

  if (!manifest.entries.empty()) {
    std::cout << "\nEntries:\n";
    for (const auto& entry : manifest.entries) {
      std::cout << "  - " << entry.relative_path
                << " [" << (entry.is_directory ? "dir" : "file") << "]"
                << " size=" << entry.size_bytes << "\n";
    }
  }
}

void print_staged_share_text(const StagedFileShare& staged) {
  std::cout << "Session:     " << staged.session_id << "\n";
  std::cout << "Stage path:  " << staged.stage_path << "\n";
  std::cout << "Manifest:    " << staged.manifest_path << "\n";
  std::cout << "Metadata:    " << staged.metadata_path << "\n";
  std::cout << "Payload:     " << staged.payload_path << "\n";
  std::cout << "Checksums:   " << (staged.checksum_path.empty() ? "<none>" : staged.checksum_path) << "\n";
  std::cout << "Copied files:" << staged.copied_file_count << "\n";
  std::cout << "Copied bytes:" << staged.copied_bytes << "\n\n";
  print_file_manifest_text(staged.manifest);
  std::cout << "\nNext step:   hyprshare session serve " << staged.session_id << " --generate-token\n";
}

void print_session_list_text(const std::vector<StagedFileShare>& sessions) {
  if (sessions.empty()) {
    std::cout << "No staged share sessions.\n";
    return;
  }

  for (const auto& session : sessions) {
    std::cout << session.session_id << " "
              << "[" << hyprshare::model::to_string(session.manifest.capability) << "] "
              << session.manifest.share_name << " files=" << session.manifest.file_count
              << " bytes=" << session.manifest.total_bytes << "\n";
  }
}

void print_deleted_session_text(const std::string_view session_id, const std::filesystem::path& stage_path) {
  std::cout << "Deleted session: " << session_id << "\n";
  std::cout << "Stage path:       " << stage_path << "\n";
}

void print_pruned_sessions_text(const std::vector<std::filesystem::path>& pruned_sessions) {
  if (pruned_sessions.empty()) {
    std::cout << "No staged share sessions matched prune criteria.\n";
    return;
  }

  std::cout << "Pruned sessions: " << pruned_sessions.size() << "\n";
  for (const auto& stage_path : pruned_sessions) {
    std::cout << "  - " << stage_path.filename().string() << " (" << stage_path << ")\n";
  }
}

void print_exported_share_text(
  const std::string_view session_id,
  const std::filesystem::path& stage_path,
  const std::filesystem::path& bundle_path) {
  std::cout << "Session:     " << session_id << "\n";
  std::cout << "Stage path:  " << stage_path << "\n";
  std::cout << "Bundle path: " << bundle_path << "\n";
}

void print_remote_offer_text(const RemoteFileShareOffer& offer) {
  std::cout << "Session:     " << offer.session_id << "\n";
  std::cout << "Endpoint:    " << offer.host << ":" << offer.port << "\n";
  if (!offer.sender_device_id.empty()) {
    std::cout << "Sender:      " << offer.sender_device_label << " (" << offer.sender_device_id << ")\n";
  }
  if (!offer.allowed_peer_id.empty()) {
    std::cout << "Allowed peer:" << offer.allowed_peer_id << "\n";
  }
  std::cout << "Capability:  " << hyprshare::model::to_string(offer.manifest.capability) << "\n";
  std::cout << "Name:        " << offer.manifest.share_name << "\n";
  std::cout << "Auth:        " << (offer.auth_required ? "token-required" : "none") << "\n";
  if (offer.auth_expires_at_unix > 0) {
    std::cout << "Auth expiry: " << offer.auth_expires_at_unix << "\n";
  }
  std::cout << "Read-only:   " << (offer.manifest.read_only ? "yes" : "no") << "\n";
  std::cout << "Files:       " << offer.manifest.file_count << "\n";
  std::cout << "Directories: " << offer.manifest.directory_count << "\n";
  std::cout << "Total bytes: " << offer.manifest.total_bytes << "\n";
  std::cout << "Bundle size: " << offer.bundle_size << "\n";
  if (!offer.manifest.notes.empty()) {
    std::cout << "\nNotes:\n";
    for (const auto& note : offer.manifest.notes) {
      std::cout << "  - " << note << "\n";
    }
  }
}

void print_probe_text(
  const std::string_view host,
  const std::uint16_t port,
  const bool reachable,
  const std::string_view error) {
  std::cout << "Endpoint:  " << host << ":" << port << "\n";
  std::cout << "Reachable: " << (reachable ? "yes" : "no") << "\n";
  if (!error.empty()) {
    std::cout << "Error:     " << error << "\n";
  }
}

void print_served_share_text(
  const std::string_view session_id,
  const std::string_view host,
  const std::uint16_t port,
  const std::uintmax_t bytes_sent) {
  std::cout << "Session:    " << session_id << "\n";
  std::cout << "Endpoint:   " << host << ":" << port << "\n";
  std::cout << "Transferred:" << bytes_sent << "\n";
}

void print_verified_share_text(const StagedFileShare& staged) {
  std::cout << "Session:      " << staged.session_id << "\n";
  std::cout << "Payload:      " << staged.payload_path << "\n";
  std::cout << "Checksums:    " << (staged.checksum_path.empty() ? "<none>" : staged.checksum_path) << "\n";
  std::cout << "Verification: ok\n";
}

void print_received_share_text(const ReceivedFileShare& received) {
  std::cout << "Session:      " << received.session_id << "\n";
  std::cout << "Destination:  " << received.destination_path << "\n";
  std::cout << "Copied files: " << received.copied_file_count << "\n";
  std::cout << "Copied bytes: " << received.copied_bytes << "\n";
}

void print_support_bundle_text(const SupportBundle& bundle) {
  std::cout << "Support bundle: " << bundle.directory << "\n";
  std::cout << "Files:\n";
  for (const auto& file : bundle.files) {
    std::cout << "  - " << file.filename().string() << "\n";
  }
}

void print_inbox_serve_hint(
  const std::string_view host,
  const std::uint16_t port,
  const std::filesystem::path& destination,
  const std::uint32_t upload_count,
  const std::uint32_t idle_timeout_seconds,
  const std::uintmax_t max_upload_bytes,
  const std::string_view token) {
  std::cout << "Inbox listening on " << host << ":" << port;
  if (upload_count == 0) {
    std::cout << " until idle timeout";
  } else {
    std::cout << " for " << upload_count << " HTTP PUT upload" << (upload_count == 1 ? "" : "s");
  }
  std::cout << "...\n";
  std::cout << "Destination: " << destination << "\n";
  if (idle_timeout_seconds == 0) {
    std::cout << "Timeout:    disabled\n";
  } else {
    std::cout << "Timeout:    " << idle_timeout_seconds << "s idle\n";
  }
  if (max_upload_bytes == 0) {
    std::cout << "Max bytes:  unlimited\n";
  } else {
    std::cout << "Max bytes:  " << max_upload_bytes << "\n";
  }
  std::cout << "Token:      " << (token.empty() ? "not required" : "required") << "\n\n";

  const auto hosts = inbox_advertised_hosts(host);
  std::cout << "Sender examples from another device on the same LAN:\n";
  for (const auto& advertised_host : hosts) {
    std::cout << "  curl --upload-file <file>";
    if (!token.empty()) {
      std::cout << " -H " << hyprshare::util::shell_quote("X-Hyprshare-Token: " + std::string(token));
    }
    std::cout << " http://" << advertised_host << ":" << port << "/<file>\n";

    std::cout << "  hyprshare inbox put " << advertised_host << ":" << port << " <path...>";
    if (!token.empty()) {
      std::cout << " --token " << hyprshare::util::shell_quote(token);
    }
    std::cout << "\n";
  }
  std::cout << "\nReceiver helpers:\n";
  std::cout << "  hyprshare inbox status\n";
  std::cout << "  hyprshare inbox open\n";

  // The process is about to block, and the output may be redirected to a
  // file, where the stream is fully buffered.
  std::cout.flush();
}

void print_session_serve_token_hint(
  const std::string_view host,
  const std::uint16_t port,
  const std::string_view token,
  const std::uint32_t token_ttl_seconds,
  const std::string_view allowed_peer_id) {
  const auto hosts = inbox_advertised_hosts(host);
  std::cout << "Generated session token: " << token << "\n";
  if (token_ttl_seconds > 0) {
    std::cout << "Token TTL: " << token_ttl_seconds << "s\n";
  }
  if (!allowed_peer_id.empty()) {
    std::cout << "Allowed peer: " << allowed_peer_id << "\n";
  }

  std::cout << "Receiver commands:\n";
  for (const auto& advertised_host : hosts) {
    std::cout << "  hyprshare session inspect " << advertised_host << ":" << port
              << " --token " << hyprshare::util::shell_quote(token);
    if (!allowed_peer_id.empty()) {
      std::cout << " --with-self";
    }
    std::cout << "\n";

    std::cout << "  hyprshare session fetch " << advertised_host << ":" << port
              << " --token " << hyprshare::util::shell_quote(token);
    if (!allowed_peer_id.empty()) {
      std::cout << " --with-self";
    }
    std::cout << " --to <path>\n";
  }
  std::cout << "\nWaiting for receiver...\n";
  std::cout.flush();
}

}  // namespace hyprshare::cli
