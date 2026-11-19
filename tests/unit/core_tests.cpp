#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <chrono>
#include <thread>

#include "hyprshare/core/adapter_registry.hpp"
#include "hyprshare/core/backend_registry.hpp"
#include "hyprshare/core/clipboard_share.hpp"
#include "hyprshare/core/config.hpp"
#include "hyprshare/core/discovery.hpp"
#include "hyprshare/core/file_share.hpp"
#include "hyprshare/core/inbox.hpp"
#include "hyprshare/core/inbox_history.hpp"
#include "hyprshare/core/planner.hpp"
#include "hyprshare/core/trust_store.hpp"
#include "hyprshare/tls/tls.hpp"
#include "hyprshare/tls/tls_transport.hpp"
#include "hyprshare/diagnostics/doctor.hpp"
#include "hyprshare/diagnostics/support_bundle.hpp"
#include "hyprshare/model/types.hpp"
#include "hyprshare/platform/paths.hpp"
#include "hyprshare/platform/process.hpp"
#include "test_support.hpp"

namespace {

using test_support::expect;

std::expected<void, std::string> send_incomplete_inbox_upload(
  const std::uint16_t port,
  const std::string_view remote_name,
  const std::string_view partial_body,
  const std::uintmax_t declared_size,
  const std::string_view sha256 = {}) {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return std::unexpected("failed to create test socket: " + std::string(std::strerror(errno)));
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
    close(fd);
    return std::unexpected("failed to build test loopback address");
  }
  if (connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
    close(fd);
    return std::unexpected("failed to connect test socket: " + std::string(std::strerror(errno)));
  }

  const std::string request = "PUT /" + std::string(remote_name) + " HTTP/1.1\r\nHost: 127.0.0.1:" +
                              std::to_string(port) + "\r\nContent-Length: " + std::to_string(declared_size) + "\r\n" +
                              (sha256.empty() ? std::string() : "X-Hyprshare-Sha256: " + std::string(sha256) + "\r\n") +
                              "Connection: close\r\n\r\n" + std::string(partial_body);
  const auto sent = send(fd, request.data(), request.size(), MSG_NOSIGNAL);
  close(fd);
  if (sent < 0) {
    return std::unexpected("failed to send incomplete test upload: " + std::string(std::strerror(errno)));
  }
  return {};
}

}  // namespace

int main() {
  const auto paths = hyprshare::platform::resolve_paths();
  hyprshare::platform::ProcessRunner runner;
  const auto temp_root = std::filesystem::temp_directory_path() / "hyprshare_test_files";
  std::filesystem::remove_all(temp_root);
  std::filesystem::create_directories(temp_root / "folder" / "nested");
  std::filesystem::create_directories(temp_root / "state");
  std::filesystem::create_directories(temp_root / "inbox");

  {
    std::ofstream single(temp_root / "hello.txt");
    single << "hello world";
  }

  {
    std::ofstream nested_file(temp_root / "folder" / "nested" / "notes.txt");
    nested_file << "notes";
  }

  {
    std::ofstream root_file(temp_root / "folder" / "root.txt");
    root_file << "root";
  }

  const auto config = hyprshare::core::load_config(paths);
  expect(config.has_value(), "default config should load");

  hyprshare::platform::Paths trust_paths = paths;
  trust_paths.state_dir = temp_root / "trust-state";
  const auto self_identity = hyprshare::core::load_or_create_device_identity(trust_paths);
  expect(self_identity.has_value(), "device identity should be created on first access");
  if (self_identity.has_value()) {
    expect(!self_identity->device_id.empty(), "device identity should have a non-empty device id");
    expect(!self_identity->device_label.empty(), "device identity should have a non-empty device label");

    const auto loaded_identity = hyprshare::core::load_or_create_device_identity(trust_paths);
    expect(loaded_identity.has_value(), "device identity should be reloadable");
    if (loaded_identity.has_value()) {
      expect(loaded_identity->device_id == self_identity->device_id, "device identity should remain stable after reload");
      expect(loaded_identity->device_label == self_identity->device_label, "device label should remain stable after reload");
    }

    const auto initial_peers = hyprshare::core::list_trusted_peers(trust_paths);
    expect(initial_peers.has_value(), "empty trusted peer list should load");
    if (initial_peers.has_value()) {
      expect(initial_peers->empty(), "trusted peer list should start empty");
    }

    const auto added_peer = hyprshare::core::add_trusted_peer("debian-laptop-01", "Debian Laptop", trust_paths);
    expect(added_peer.has_value(), "manual trusted peer add should succeed");
    if (added_peer.has_value()) {
      expect(added_peer->device_id == "debian-laptop-01", "trusted peer add should preserve device id");
      expect(added_peer->device_label == "Debian Laptop", "trusted peer add should preserve device label");
    }

    const auto duplicate_peer = hyprshare::core::add_trusted_peer("debian-laptop-01", "Duplicate", trust_paths);
    expect(!duplicate_peer.has_value(), "duplicate trusted peer add should fail");

    const auto self_peer =
      hyprshare::core::add_trusted_peer(self_identity->device_id, self_identity->device_label, trust_paths);
    expect(!self_peer.has_value(), "local device should not be addable as a remote trusted peer");

    const auto listed_peers = hyprshare::core::list_trusted_peers(trust_paths);
    expect(listed_peers.has_value(), "trusted peer list should load after add");
    if (listed_peers.has_value()) {
      expect(listed_peers->size() == 1, "trusted peer list should contain the added peer");
      if (!listed_peers->empty()) {
        expect(listed_peers->front().device_id == "debian-laptop-01", "trusted peer list should expose the added device id");
      }
    }

    const auto exported_peer_card = hyprshare::core::export_device_identity(trust_paths, temp_root / "self.peer");
    expect(exported_peer_card.has_value(), "device identity should export into a peer card");
    if (exported_peer_card.has_value()) {
      const auto inspected_peer_card = hyprshare::core::inspect_peer_card(*exported_peer_card);
      expect(inspected_peer_card.has_value(), "peer card should be inspectable before import");
      if (inspected_peer_card.has_value()) {
        expect(inspected_peer_card->device_id == self_identity->device_id, "peer card inspect should preserve device id");
        expect(inspected_peer_card->device_label == self_identity->device_label, "peer card inspect should preserve device label");
      }

      hyprshare::platform::Paths importer_paths = paths;
      importer_paths.state_dir = temp_root / "importer-state";
      const auto importer_peers_before = hyprshare::core::list_trusted_peers(importer_paths);
      expect(importer_peers_before.has_value(), "importer trust store should list before pair");
      if (importer_peers_before.has_value()) {
        expect(importer_peers_before->empty(), "peer card inspect should not modify the importer trust store");
      }

      const auto imported_peer = hyprshare::core::import_trusted_peer(*exported_peer_card, importer_paths);
      expect(imported_peer.has_value(), "peer card should import into another trust store");
      if (imported_peer.has_value()) {
        expect(imported_peer->device_id == self_identity->device_id, "imported peer card should preserve device id");
        expect(imported_peer->device_label == self_identity->device_label, "imported peer card should preserve device label");
      }

      const auto self_import = hyprshare::core::import_trusted_peer(*exported_peer_card, trust_paths);
      expect(!self_import.has_value(), "local device should not be importable into its own trust store");
    }

    const auto removed_peer = hyprshare::core::remove_trusted_peer("debian-laptop-01", trust_paths);
    expect(removed_peer.has_value(), "trusted peer remove should succeed");
    const auto peers_after_remove = hyprshare::core::list_trusted_peers(trust_paths);
    expect(peers_after_remove.has_value(), "trusted peer list should load after remove");
    if (peers_after_remove.has_value()) {
      expect(peers_after_remove->empty(), "trusted peer list should be empty after remove");
    }

    hyprshare::platform::Paths invite_source_paths = paths;
    invite_source_paths.state_dir = temp_root / "invite-source-state";
    hyprshare::platform::Paths invite_target_paths = paths;
    invite_target_paths.state_dir = temp_root / "invite-target-state";

    const auto invite_source_identity = hyprshare::core::load_or_create_device_identity(invite_source_paths);
    const auto invite_target_identity = hyprshare::core::load_or_create_device_identity(invite_target_paths);
    expect(invite_source_identity.has_value(), "invite source identity should be creatable");
    expect(invite_target_identity.has_value(), "invite target identity should be creatable");

    if (invite_source_identity.has_value() && invite_target_identity.has_value()) {
      const auto request_invite_path =
        hyprshare::core::create_peer_invite(invite_source_paths, temp_root / "invite.request");
      expect(request_invite_path.has_value(), "peer invite request should be creatable");
      if (request_invite_path.has_value()) {
        const auto inspected_request = hyprshare::core::inspect_peer_invite(*request_invite_path);
        expect(inspected_request.has_value(), "peer invite request should be inspectable");
        if (inspected_request.has_value()) {
          expect(inspected_request->kind == hyprshare::model::PeerInviteKind::Request,
                 "created invite should be typed as a request");
          expect(inspected_request->from_device_id == invite_source_identity->device_id,
                 "invite request should preserve the source device id");
          expect(inspected_request->target_device_id.empty(), "invite request should not declare a target device id");
        }

        const auto target_peers_before = hyprshare::core::list_trusted_peers(invite_target_paths);
        expect(target_peers_before.has_value(), "invite target peer list should load before respond");
        if (target_peers_before.has_value()) {
          expect(target_peers_before->empty(), "invite inspect should not modify the target trust store");
        }

        const auto response_invite_path = hyprshare::core::respond_to_peer_invite(
          *request_invite_path,
          invite_target_paths,
          temp_root / "invite.response");
        expect(response_invite_path.has_value(), "peer invite response should be creatable");
        if (response_invite_path.has_value()) {
          const auto inspected_response = hyprshare::core::inspect_peer_invite(*response_invite_path);
          expect(inspected_response.has_value(), "peer invite response should be inspectable");
          if (inspected_response.has_value()) {
            expect(inspected_response->kind == hyprshare::model::PeerInviteKind::Response,
                   "responded invite should be typed as a response");
            expect(inspected_response->from_device_id == invite_target_identity->device_id,
                   "invite response should preserve the responder device id");
            expect(inspected_response->target_device_id == invite_source_identity->device_id,
                   "invite response should target the original requester");
          }

          const auto target_peers_after = hyprshare::core::list_trusted_peers(invite_target_paths);
          expect(target_peers_after.has_value(), "invite target peer list should load after respond");
          if (target_peers_after.has_value()) {
            expect(target_peers_after->size() == 1, "invite respond should trust the original requester");
            if (!target_peers_after->empty()) {
              expect(target_peers_after->front().device_id == invite_source_identity->device_id,
                     "invite respond should add the requester to the target trust store");
            }
          }

          const auto source_peers_before = hyprshare::core::list_trusted_peers(invite_source_paths);
          expect(source_peers_before.has_value(), "invite source peer list should load before complete");
          if (source_peers_before.has_value()) {
            expect(source_peers_before->empty(), "invite inspect should not modify the source trust store");
          }

          const auto completed_invite = hyprshare::core::complete_peer_invite(*response_invite_path, invite_source_paths);
          expect(completed_invite.has_value(), "peer invite response should complete into a trusted peer");
          if (completed_invite.has_value()) {
            expect(completed_invite->device_id == invite_target_identity->device_id,
                   "peer invite completion should trust the responder");
            expect(completed_invite->device_label == invite_target_identity->device_label,
                   "peer invite completion should preserve the responder label");
          }

          const auto source_peers_after = hyprshare::core::list_trusted_peers(invite_source_paths);
          expect(source_peers_after.has_value(), "invite source peer list should load after complete");
          if (source_peers_after.has_value()) {
            expect(source_peers_after->size() == 1, "invite complete should add the responder to the source trust store");
            if (!source_peers_after->empty()) {
              expect(source_peers_after->front().device_id == invite_target_identity->device_id,
                     "invite complete should add the responder device id");
            }
          }
        }
      }
    }
  }

  const auto backends = hyprshare::core::BackendRegistry::load(paths.data_dir / "backends");
  expect(backends.has_value(), "backends should load");
  if (backends.has_value()) {
    expect(backends->all().size() >= 2, "expected at least two shipped backends");
    expect(backends->find("portal-screencast").has_value(), "portal-screencast backend should exist");
    expect(backends->find("obs-virtual-camera")->launch_supported, "OBS backend should be launch-capable");
  }

  const auto adapters = hyprshare::core::AdapterRegistry::load(paths.data_dir / "compat" / "apps");
  expect(adapters.has_value(), "adapter registry should load");
  if (adapters.has_value()) {
    expect(adapters->find_by_hint("wemeet").has_value(), "wemeet profile should be discoverable");
    const auto wemeet = adapters->find_by_hint("wemeet");
    expect(wemeet->launch_commands.size() >= 1, "wemeet profile should declare launch commands");
  }

  if (config.has_value() && backends.has_value() && adapters.has_value()) {
    const auto discovery_status = hyprshare::core::inspect_discovery_status(runner);
    expect(discovery_status.ready_to_announce ==
             (discovery_status.avahi_publish_service_available && discovery_status.avahi_daemon_active),
           "discovery announce readiness should match Avahi publish tool and daemon state");
    expect(discovery_status.ready_to_browse ==
             (discovery_status.avahi_browse_available && discovery_status.avahi_daemon_active),
           "discovery browse readiness should match Avahi browse tool and daemon state");

    const auto announce_plan = hyprshare::core::plan_inbox_announce("hyprshare test inbox", 28556, true);
    expect(announce_plan.name == "hyprshare test inbox", "inbox announce plan should preserve the service name");
    expect(announce_plan.service_type == "_hyprshare._tcp", "inbox announce plan should use the hyprshare mDNS service type");
    expect(announce_plan.port == 28556, "inbox announce plan should preserve the configured port");
    expect(announce_plan.token_required, "inbox announce plan should preserve token-required state");
    expect(announce_plan.command.find("avahi-publish-service") != std::string::npos,
           "inbox announce plan should use Avahi publish tooling");
    expect(announce_plan.command.find("token=required") != std::string::npos,
           "inbox announce plan should advertise token-required metadata without publishing the token");

    const std::string avahi_output =
      "=;wlp4s0;IPv4;Arch Hyprland Inbox;_hyprshare._tcp;local;arch.local;192.168.1.42;28556;"
      "\"version=1\";\"capability=inbox-put\";\"protocol=http-put\";\"token=required\"\n";
    const auto discovered_inboxes = hyprshare::core::parse_avahi_browse_output(avahi_output);
    expect(discovered_inboxes.has_value(), "Avahi browse output should parse");
    if (discovered_inboxes.has_value()) {
      expect(discovered_inboxes->size() == 1, "Avahi browse output should expose one discovered inbox");
      if (!discovered_inboxes->empty()) {
        expect(discovered_inboxes->front().name == "Arch Hyprland Inbox", "discovered inbox should preserve service name");
        expect(discovered_inboxes->front().address == "192.168.1.42", "discovered inbox should preserve address");
        expect(discovered_inboxes->front().port == 28556, "discovered inbox should preserve port");
        expect(discovered_inboxes->front().token_required, "discovered inbox should parse token-required metadata");
        const auto selected_discovered =
          hyprshare::core::select_discovered_inbox(*discovered_inboxes, "Arch Hyprland Inbox");
        expect(selected_discovered.has_value(), "discovered inbox should be selectable by service name");
        if (selected_discovered.has_value()) {
          expect(selected_discovered->address == "192.168.1.42", "selected discovered inbox should preserve address");
        }
        const auto missing_discovered =
          hyprshare::core::select_discovered_inbox(*discovered_inboxes, "Missing Inbox");
        expect(!missing_discovered.has_value(), "missing discovered inbox selection should fail");
      }
    }

    hyprshare::platform::Paths inbox_history_paths = paths;
    inbox_history_paths.state_dir = temp_root / "inbox-history-state";
    const hyprshare::model::InboxReceivedFile first_history_file{
      .file_name = "photo.jpg",
      .destination_path = (temp_root / "inbox" / "photo.jpg").string(),
      .bytes_received = 1234,
      .sha256 = "",
    };
    const hyprshare::model::InboxReceivedFile second_history_file{
      .file_name = "notes.txt",
      .destination_path = (temp_root / "inbox" / "notes.txt").string(),
      .bytes_received = 456,
      .sha256 = "",
    };
    const auto first_history_entry = hyprshare::core::append_inbox_history(first_history_file, inbox_history_paths);
    const auto second_history_entry = hyprshare::core::append_inbox_history(second_history_file, inbox_history_paths);
    expect(first_history_entry.has_value(), "first inbox history entry should be writable");
    expect(second_history_entry.has_value(), "second inbox history entry should be writable");
    const auto listed_history = hyprshare::core::list_inbox_history(inbox_history_paths);
    expect(listed_history.has_value(), "inbox history should be listable");
    if (listed_history.has_value()) {
      expect(listed_history->size() == 2, "inbox history should contain both recorded entries");
      if (listed_history->size() == 2) {
        expect(listed_history->at(0).file_name == "notes.txt", "inbox history should sort the newest entry first");
        expect(listed_history->at(1).file_name == "photo.jpg", "inbox history should keep the older entry second");
      }
    }
    const auto cleared_history = hyprshare::core::clear_inbox_history(inbox_history_paths);
    expect(cleared_history.has_value(), "inbox history should be clearable");
    if (cleared_history.has_value()) {
      expect(*cleared_history == 2, "clearing inbox history should remove all recorded entries");
    }
    const auto empty_history = hyprshare::core::list_inbox_history(inbox_history_paths);
    expect(empty_history.has_value(), "inbox history should remain listable after clear");
    if (empty_history.has_value()) {
      expect(empty_history->empty(), "inbox history should be empty after clear");
    }

    hyprshare::diagnostics::Doctor doctor(runner);
    hyprshare::platform::Paths doctor_paths = paths;
    doctor_paths.config_dir = temp_root / "doctor-config";
    doctor_paths.config_file = doctor_paths.config_dir / "hyprshare.conf";
    doctor_paths.state_dir = temp_root / "doctor-state";
    std::filesystem::create_directories(doctor_paths.config_dir);
    {
      std::ofstream doctor_config_file(doctor_paths.config_file);
      doctor_config_file << "[files]\n";
      doctor_config_file << "receive_dir = \"" << (temp_root / "doctor-inbox").string() << "\"\n";
    }
    const auto doctor_config = hyprshare::core::load_config(doctor_paths);
    expect(doctor_config.has_value(), "doctor config should load");

    const auto doctor_report = doctor.run(doctor_paths, *adapters, std::optional<std::string_view>("wemeet"));
    expect(doctor_report.facts.contains("tar_available"), "doctor report should expose tar availability");
    expect(doctor_report.facts.contains("sha256sum_available"), "doctor report should expose sha256sum availability");
    expect(doctor_report.facts.contains("receive_dir_ready"), "doctor report should expose receive directory readiness");
    expect(doctor_report.facts.at("config_loaded"), "doctor report should load the temporary config");
    expect(doctor_report.facts.at("tar_available") == runner.command_exists("tar"),
           "doctor tar fact should match command detection");
    expect(doctor_report.facts.at("sha256sum_available") == runner.command_exists("sha256sum"),
           "doctor sha256sum fact should match command detection");
    expect(doctor_report.facts.at("receive_dir_ready"),
           "doctor report should consider a writable temporary receive directory ready");
    bool found_receive_dir_check = false;
    for (const auto& check : doctor_report.checks) {
      if (check.id == "files.receive_dir") {
        found_receive_dir_check = true;
        expect(check.status == hyprshare::model::CheckStatus::Pass,
               "doctor receive directory check should pass for a writable temporary directory");
      }
    }
    expect(found_receive_dir_check, "doctor report should include a receive directory check");
    hyprshare::diagnostics::SupportBundleOptions support_options;
    support_options.output_dir = temp_root / "support-bundle";
    support_options.app_hint = "wemeet";
    const auto support_bundle =
      hyprshare::diagnostics::write_support_bundle(
        doctor_paths, doctor_config.value_or(hyprshare::core::Config{}), *backends, *adapters, doctor_report, support_options);
    expect(support_bundle.has_value(), "support bundle should be writable");
    if (support_bundle.has_value()) {
      expect(std::filesystem::exists(support_bundle->directory / "summary.txt"), "support bundle should contain summary.txt");
      expect(std::filesystem::exists(support_bundle->directory / "doctor.txt"), "support bundle should contain doctor.txt");
      expect(std::filesystem::exists(support_bundle->directory / "backends.txt"), "support bundle should contain backends.txt");
      expect(std::filesystem::exists(support_bundle->directory / "compat-apps.txt"),
             "support bundle should contain compat-apps.txt");
      expect(std::filesystem::exists(support_bundle->directory / "manifest.txt"), "support bundle should contain manifest.txt");
    }

    hyprshare::model::FactMap portal_ready{
      {"wayland_session", true},
      {"pipewire_active", true},
      {"wireplumber_active", true},
      {"xdg_desktop_portal_active", true},
      {"xdph_active", true},
      {"obs_available", false},
      {"v4l2loopback_present", false},
    };

    hyprshare::core::SharePlanner planner;
    const auto portal_plan = planner.plan_screen("wemeet", portal_ready, *config, *backends, *adapters);
    expect(portal_plan.selected_backend_id == "portal-screencast", "portal path should be preferred when healthy");

    hyprshare::model::FactMap obs_only{
      {"wayland_session", true},
      {"pipewire_active", false},
      {"wireplumber_active", false},
      {"xdg_desktop_portal_active", false},
      {"xdph_active", false},
      {"obs_available", true},
      {"v4l2loopback_present", true},
    };
    const auto obs_plan = planner.plan_screen("wemeet", obs_only, *config, *backends, *adapters);
    expect(obs_plan.selected_backend_id == "obs-virtual-camera", "OBS fallback should be selected when portal path is broken");

    hyprshare::model::FactMap launch_ready{
      {"wayland_session", true},
      {"pipewire_active", true},
      {"wireplumber_active", true},
      {"xdg_desktop_portal_active", true},
      {"xdph_active", true},
      {"obs_available", true},
      {"v4l2loopback_present", true},
    };
    const std::vector<std::string> launch_commands{"wemeetapp"};
    const auto launch_plan = planner.plan_app_launch("wemeet", launch_ready, *config, *backends, *adapters, launch_commands);
    expect(launch_plan.ready, "app launch plan should be ready when executable and helper backend exist");
    expect(launch_plan.selected_backend_id == "obs-virtual-camera", "app launch should prefer orchestrable OBS fallback");
    expect(!launch_plan.actions.empty(), "app launch plan should contain actions");
    expect(launch_plan.actions.front().kind == hyprshare::model::LaunchActionKind::StartHelper,
           "first launch action should start the helper backend");

    const std::vector<std::string> portal_only_commands{"zoom"};
    hyprshare::model::FactMap portal_only{
      {"wayland_session", true},
      {"pipewire_active", true},
      {"wireplumber_active", true},
      {"xdg_desktop_portal_active", true},
      {"xdph_active", true},
      {"obs_available", false},
      {"v4l2loopback_present", false},
      {"xwaylandvideobridge_available", false},
    };
    const auto portal_launch = planner.plan_app_launch("zoom", portal_only, *config, *backends, *adapters, portal_only_commands);
    expect(portal_launch.ready, "portal launch plan should still be executable when the app command exists");
    expect(portal_launch.selected_backend_id == "portal-screencast", "portal backend should be retained when no launch helper is viable");

    const auto file_manifest = hyprshare::core::build_file_manifest(temp_root / "hello.txt");
    expect(file_manifest.has_value(), "single-file manifest should build");
    if (file_manifest.has_value()) {
      expect(file_manifest->capability == hyprshare::model::CapabilityKind::File, "file manifest should be typed as file");
      expect(file_manifest->file_count == 1, "single-file manifest should count one file");
      expect(file_manifest->entries.size() == 1, "single-file manifest should contain one entry");
    }

    const auto folder_manifest = hyprshare::core::build_folder_manifest(temp_root / "folder", config->files);
    expect(folder_manifest.has_value(), "folder manifest should build");
    if (folder_manifest.has_value()) {
      expect(folder_manifest->capability == hyprshare::model::CapabilityKind::Folder, "folder manifest should be typed as folder");
      expect(folder_manifest->file_count == 2, "folder manifest should count nested files");
      expect(folder_manifest->directory_count >= 1, "folder manifest should count nested directories");
      expect(folder_manifest->entries.size() >= 3, "folder manifest should contain directory and file entries");
      expect(folder_manifest->read_only == config->files.folder_shares_read_only_by_default,
             "folder manifest should honor read-only default");
    }

    const auto missing_manifest = hyprshare::core::build_file_manifest(temp_root / "missing.txt");
    expect(!missing_manifest.has_value(), "missing file should fail manifest creation");

    {
      const auto clipboard_text_path = temp_root / "clipboard.txt";
      {
        std::ofstream clipboard_file(clipboard_text_path);
        clipboard_file << "clipboard text\nwith newline";
      }

      const auto clipboard = hyprshare::core::load_clipboard_text_file(clipboard_text_path);
      expect(clipboard.has_value(), "clipboard text file should load into a clipboard snapshot");
      if (clipboard.has_value()) {
        expect(clipboard->text == "clipboard text\nwith newline", "clipboard text file should preserve text content");
        expect(clipboard->backend == "file", "clipboard text file backend should be marked as file");
        expect(clipboard->byte_count == clipboard->text.size(), "clipboard byte count should match loaded text");

        hyprshare::platform::Paths clipboard_paths = paths;
        clipboard_paths.state_dir = temp_root / "clipboard-state";
        clipboard_paths.cache_dir = temp_root / "clipboard-cache";
        clipboard_paths.runtime_dir = temp_root / "clipboard-runtime";

        const auto staged_clipboard = hyprshare::core::stage_clipboard_text(*clipboard, clipboard_paths, runner);
        expect(staged_clipboard.has_value(), "clipboard snapshot should stage into a file-share session");
        if (staged_clipboard.has_value()) {
          expect(staged_clipboard->manifest.capability == hyprshare::model::CapabilityKind::Clipboard,
                 "staged clipboard manifest should be typed as clipboard");
          expect(staged_clipboard->manifest.file_count == 1, "staged clipboard manifest should contain one file");
          expect(std::filesystem::exists(std::filesystem::path(staged_clipboard->payload_path) / "clipboard.txt"),
                 "staged clipboard payload should use a stable clipboard filename");

          const auto received_clipboard = hyprshare::core::clipboard_from_staged_share(*staged_clipboard);
          expect(received_clipboard.has_value(), "staged clipboard session should load back into clipboard text");
          if (received_clipboard.has_value()) {
            expect(received_clipboard->text == clipboard->text, "staged clipboard text should round-trip");
            expect(received_clipboard->backend == "staged-session", "received clipboard backend should be staged-session");
          }
        }
      }
    }

    {
      const auto upload_path = temp_root / "upload-source.txt";
      const auto second_upload_path = temp_root / "upload-second.txt";
      const auto upload_dir = temp_root / "upload-folder";
      {
        std::ofstream upload_file(upload_path);
        upload_file << "uploaded from another device";
      }
      {
        std::ofstream second_upload_file(second_upload_path);
        second_upload_file << "second upload";
      }
      std::filesystem::create_directories(upload_dir / "nested");
      {
        std::ofstream directory_file(upload_dir / "nested" / "folder-note.txt");
        directory_file << "folder upload";
      }

      constexpr std::uint16_t inbox_port = 28452;
      const bool tar_available = runner.command_exists("tar");
      const std::uint32_t expected_upload_count = tar_available ? 3U : 2U;
      std::expected<std::vector<hyprshare::model::InboxReceivedFile>, std::string> inbox_received =
        std::unexpected(std::string("not started"));
      std::thread inbox_server([&]() {
        inbox_received = hyprshare::core::serve_inbox(
          "127.0.0.1",
          inbox_port,
          temp_root / "inbox-http",
          expected_upload_count,
          "inbox-token",
          3,
          0,
          &runner);
      });
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      const auto uploaded = hyprshare::core::put_inbox_file(
        "localhost",
        inbox_port,
        upload_path,
        "../phone upload.txt",
        "inbox-token");
      const auto second_uploaded = hyprshare::core::put_inbox_file(
        "127.0.0.1",
        inbox_port,
        second_upload_path,
        "second.txt",
        "inbox-token");
      std::expected<hyprshare::core::InboxUploadResult, std::string> directory_uploaded =
        std::unexpected(std::string("tar not available"));
      if (tar_available) {
        directory_uploaded = hyprshare::core::put_inbox_path(
          "127.0.0.1",
          inbox_port,
          upload_dir,
          runner,
          "",
          "inbox-token");
      }
      inbox_server.join();

      expect(uploaded.has_value(), "inbox HTTP PUT helper should upload a file");
      expect(second_uploaded.has_value(), "inbox HTTP PUT helper should upload a second file");
      if (tar_available) {
        expect(directory_uploaded.has_value(), "inbox HTTP PUT helper should archive and upload a directory");
      }
      expect(inbox_received.has_value(), "inbox receiver should accept multiple HTTP PUT uploads");
      if (!uploaded.has_value()) {
        std::cerr << "inbox upload error: " << uploaded.error() << "\n";
      }
      if (!second_uploaded.has_value()) {
        std::cerr << "second inbox upload error: " << second_uploaded.error() << "\n";
      }
      if (tar_available && !directory_uploaded.has_value()) {
        std::cerr << "directory inbox upload error: " << directory_uploaded.error() << "\n";
      }
      if (!inbox_received.has_value()) {
        std::cerr << "inbox receive error: " << inbox_received.error() << "\n";
      }
      if (uploaded.has_value() && second_uploaded.has_value() && inbox_received.has_value()) {
        expect(inbox_received->size() == expected_upload_count, "inbox receiver should return all uploaded files");
        if (inbox_received->size() == expected_upload_count) {
          expect(*uploaded == inbox_received->at(0).bytes_received, "first inbox upload byte count should match received bytes");
          expect(*second_uploaded == inbox_received->at(1).bytes_received, "second inbox upload byte count should match received bytes");
          expect(inbox_received->at(0).file_name == "phone_upload.txt", "inbox receiver should sanitize uploaded file names");
          expect(inbox_received->at(1).file_name == "second.txt", "inbox receiver should preserve safe uploaded file names");
          expect(std::filesystem::exists(inbox_received->at(0).destination_path), "inbox receiver should write the first uploaded file");
          expect(std::filesystem::exists(inbox_received->at(1).destination_path), "inbox receiver should write the second uploaded file");
          if (tar_available && directory_uploaded.has_value()) {
            expect(directory_uploaded->archived_directory, "directory upload result should identify archive mode");
            expect(directory_uploaded->remote_name == "upload-folder.tar.gz", "directory upload should use a stable archive name");
            expect(directory_uploaded->bytes_sent == inbox_received->at(2).bytes_received,
                   "directory archive byte count should match received bytes");
            if (runner.command_exists("sha256sum")) {
              expect(!directory_uploaded->sha256.empty(), "directory upload result should include a sha256 checksum");
              expect(inbox_received->at(2).sha256 == directory_uploaded->sha256,
                     "inbox receiver should preserve the verified directory archive checksum");
            }
            expect(inbox_received->at(2).file_name == "upload-folder.tar.gz",
                   "inbox receiver should preserve directory archive file name");
            expect(std::filesystem::exists(inbox_received->at(2).destination_path),
                   "inbox receiver should write the directory archive");
            const auto dry_run_extract = hyprshare::core::extract_inbox_archive(
              inbox_received->at(2).destination_path,
              temp_root / "inbox-extracted",
              runner,
              true);
            expect(dry_run_extract.has_value(), "inbox archive extract dry-run should validate a received directory archive");
            if (dry_run_extract.has_value()) {
              expect(dry_run_extract->dry_run, "inbox archive dry-run result should be marked as dry-run");
              expect(dry_run_extract->inspection.top_level == "upload-folder",
                     "inbox archive dry-run should report the top-level directory");
              expect(!std::filesystem::exists(dry_run_extract->extracted_path),
                     "inbox archive dry-run should not create the extracted path");
            }

            const auto extracted = hyprshare::core::extract_inbox_archive(
              inbox_received->at(2).destination_path,
              temp_root / "inbox-extracted",
              runner);
            expect(extracted.has_value(), "inbox archive extract should unpack a received directory archive");
            if (extracted.has_value()) {
              expect(extracted->extracted_path.filename() == "upload-folder",
                     "inbox archive extract should preserve the uploaded directory name");
              expect(std::filesystem::exists(extracted->extracted_path / "nested" / "folder-note.txt"),
                     "inbox archive extract should restore nested files");
            }
          }
        }
      }

      constexpr std::uint16_t inbox_until_idle_port = 28454;
      std::expected<std::vector<hyprshare::model::InboxReceivedFile>, std::string> until_idle_received =
        std::unexpected(std::string("not started"));
      std::thread until_idle_server([&]() {
        until_idle_received = hyprshare::core::serve_inbox(
          "127.0.0.1",
          inbox_until_idle_port,
          temp_root / "inbox-until-idle",
          0,
          "inbox-token",
          1);
      });
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      const auto until_idle_first = hyprshare::core::put_inbox_file(
        "127.0.0.1",
        inbox_until_idle_port,
        upload_path,
        "idle-one.txt",
        "inbox-token");
      const auto until_idle_second = hyprshare::core::put_inbox_file(
        "127.0.0.1",
        inbox_until_idle_port,
        second_upload_path,
        "idle-two.txt",
        "inbox-token");
      until_idle_server.join();

      expect(until_idle_first.has_value(), "until-idle inbox receiver should accept the first upload");
      expect(until_idle_second.has_value(), "until-idle inbox receiver should accept the second upload");
      expect(until_idle_received.has_value(), "until-idle inbox receiver should stop cleanly after idle timeout");
      if (!until_idle_received.has_value()) {
        std::cerr << "until-idle inbox receive error: " << until_idle_received.error() << "\n";
      }
      if (until_idle_received.has_value()) {
        expect(until_idle_received->size() == 2, "until-idle inbox receiver should return uploads received before timeout");
        if (until_idle_received->size() == 2) {
          expect(until_idle_received->at(0).file_name == "idle-one.txt", "until-idle receiver should preserve first file name");
          expect(until_idle_received->at(1).file_name == "idle-two.txt", "until-idle receiver should preserve second file name");
        }
      }

      constexpr std::uint16_t inbox_limited_port = 28455;
      std::expected<std::vector<hyprshare::model::InboxReceivedFile>, std::string> limited_received =
        std::unexpected(std::string("not started"));
      std::thread limited_server([&]() {
        limited_received = hyprshare::core::serve_inbox(
          "127.0.0.1",
          inbox_limited_port,
          temp_root / "inbox-limited",
          1,
          "",
          3,
          4);
      });
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      const auto limited_upload = hyprshare::core::put_inbox_file(
        "127.0.0.1",
        inbox_limited_port,
        upload_path,
        "too-large.txt",
        "");
      limited_server.join();

      expect(!limited_upload.has_value(), "inbox upload should fail when receiver max-bytes is exceeded");
      expect(!limited_received.has_value(), "inbox receiver should reject uploads larger than max-bytes");
      if (!limited_received.has_value()) {
        expect(limited_received.error().find("max-bytes") != std::string::npos,
               "max-bytes rejection should explain the size limit");
      }
      expect(!std::filesystem::exists(temp_root / "inbox-limited" / "too-large.txt"),
             "max-bytes rejection should not write an oversized file");

      constexpr std::uint16_t inbox_partial_port = 28456;
      const auto partial_dir = temp_root / "inbox-partial";
      std::expected<std::vector<hyprshare::model::InboxReceivedFile>, std::string> partial_received =
        std::unexpected(std::string("not started"));
      std::thread partial_server([&]() {
        partial_received = hyprshare::core::serve_inbox(
          "127.0.0.1",
          inbox_partial_port,
          partial_dir,
          1,
          "",
          3);
      });
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      const auto incomplete_sent = send_incomplete_inbox_upload(inbox_partial_port, "partial.txt", "part", 128);
      partial_server.join();

      expect(incomplete_sent.has_value(), "test helper should send an incomplete inbox upload");
      expect(!partial_received.has_value(), "inbox receiver should reject incomplete uploads");
      if (!partial_received.has_value()) {
        expect(partial_received.error().find("client closed") != std::string::npos,
               "incomplete upload rejection should explain the closed client");
      }
      expect(std::filesystem::exists(partial_dir), "partial upload receiver should create the inbox directory");
      if (std::filesystem::exists(partial_dir)) {
        expect(std::filesystem::directory_iterator(partial_dir) == std::filesystem::directory_iterator(),
               "incomplete uploads should not leave final or temporary files in the inbox");
      }

      if (runner.command_exists("sha256sum")) {
        constexpr std::uint16_t inbox_checksum_port = 28457;
        const auto checksum_dir = temp_root / "inbox-checksum";
        std::expected<std::vector<hyprshare::model::InboxReceivedFile>, std::string> checksum_received =
          std::unexpected(std::string("not started"));
        std::thread checksum_server([&]() {
          checksum_received = hyprshare::core::serve_inbox(
            "127.0.0.1",
            inbox_checksum_port,
            checksum_dir,
            1,
            "",
            3,
            0,
            &runner);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto checksum_sent =
          send_incomplete_inbox_upload(inbox_checksum_port, "wrong-checksum.txt", "body", 4, std::string(64, '0'));
        checksum_server.join();

        expect(checksum_sent.has_value(), "test helper should send a checksum-protected inbox upload");
        expect(!checksum_received.has_value(), "inbox receiver should reject uploads with a wrong checksum");
        if (!checksum_received.has_value()) {
          expect(checksum_received.error().find("checksum mismatch") != std::string::npos,
                 "checksum rejection should explain the mismatch");
        }
        expect(std::filesystem::exists(checksum_dir), "checksum receiver should create the inbox directory");
        if (std::filesystem::exists(checksum_dir)) {
          expect(std::filesystem::directory_iterator(checksum_dir) == std::filesystem::directory_iterator(),
                 "checksum mismatch should not leave final or temporary files in the inbox");
        }
      }

      constexpr std::uint16_t inbox_timeout_port = 28453;
      const auto timed_out = hyprshare::core::serve_inbox(
        "127.0.0.1",
        inbox_timeout_port,
        temp_root / "inbox-timeout",
        1,
        "",
        1);
      expect(!timed_out.has_value(), "inbox receiver should time out when no upload arrives");
      if (!timed_out.has_value()) {
        expect(timed_out.error().find("timed out") != std::string::npos, "inbox timeout error should explain the timeout");
      }
    }

    if (folder_manifest.has_value()) {
      hyprshare::platform::Paths stage_paths = paths;
      stage_paths.state_dir = temp_root / "state";
      const auto staged = hyprshare::core::stage_file_share(*folder_manifest, stage_paths, runner);
      expect(staged.has_value(), "folder manifest should stage into a share snapshot");
      if (staged.has_value()) {
        expect(std::filesystem::exists(staged->manifest_path), "staged manifest file should exist");
        expect(std::filesystem::exists(staged->metadata_path), "staged metadata file should exist");
        expect(std::filesystem::exists(staged->payload_path), "staged payload directory should exist");
        expect(std::filesystem::exists(staged->checksum_path), "staged checksum file should exist");
        expect(std::filesystem::exists(std::filesystem::path(staged->payload_path) / "nested" / "notes.txt"),
               "staged payload should contain nested file snapshot");
        expect(staged->copied_file_count == folder_manifest->file_count, "staged file count should match manifest");
        expect(staged->copied_bytes == folder_manifest->total_bytes, "staged byte count should match manifest");

        const auto verified = hyprshare::core::verify_staged_file_share(*staged, runner);
        expect(verified.has_value(), "staged share should verify successfully");

        const auto loaded = hyprshare::core::load_staged_file_share(staged->session_id, stage_paths);
        expect(loaded.has_value(), "staged session should be loadable");
        if (loaded.has_value()) {
          expect(loaded->manifest.share_name == folder_manifest->share_name, "loaded staged session should preserve share name");
          expect(loaded->manifest.file_count == folder_manifest->file_count, "loaded staged session should preserve file count");
          expect(loaded->checksum_path == staged->checksum_path, "loaded staged session should preserve checksum path");
        }

        const auto listed = hyprshare::core::list_staged_file_shares(stage_paths);
        expect(listed.has_value(), "staged share list should load");
        if (listed.has_value()) {
          expect(!listed->empty(), "staged share list should contain the staged session");
        }

        const auto deleted_stage = hyprshare::core::stage_file_share(*folder_manifest, stage_paths, runner);
        expect(deleted_stage.has_value(), "secondary staged share should be created for delete coverage");
        if (deleted_stage.has_value()) {
          const auto deleted = hyprshare::core::delete_staged_file_share(deleted_stage->session_id, stage_paths);
          expect(deleted.has_value(), "staged share delete should succeed");
          expect(!std::filesystem::exists(deleted_stage->stage_path), "deleted staged share directory should be removed");
          const auto deleted_load = hyprshare::core::load_staged_file_share(deleted_stage->session_id, stage_paths);
          expect(!deleted_load.has_value(), "deleted staged share should no longer load");
        }

        const auto prune_candidate = hyprshare::core::stage_file_share(*folder_manifest, stage_paths, runner);
        expect(prune_candidate.has_value(), "secondary staged share should be created for prune coverage");
        if (prune_candidate.has_value()) {
          std::error_code age_error;
          std::filesystem::last_write_time(
            prune_candidate->stage_path,
            std::filesystem::file_time_type::clock::now() - std::chrono::hours(1),
            age_error);
          expect(!age_error, "prune candidate timestamp should be rewritable for age-based cleanup");

          const auto pruned = hyprshare::core::prune_staged_file_shares(stage_paths, 60);
          expect(pruned.has_value(), "age-based staged share prune should succeed");
          if (pruned.has_value()) {
            expect(pruned->size() == 1, "prune should remove exactly one aged staged share");
            if (!pruned->empty()) {
              expect(pruned->front().filename() == prune_candidate->session_id, "prune should remove the aged session");
            }
          }
          expect(!std::filesystem::exists(prune_candidate->stage_path), "pruned staged share should be removed");
          expect(std::filesystem::exists(staged->stage_path), "active staged share should remain after pruning an older session");
        }

        const auto received = hyprshare::core::receive_staged_file_share(*staged, temp_root / "inbox");
        expect(received.has_value(), "staged share should be receivable");
        if (received.has_value()) {
          expect(std::filesystem::exists(received->destination_path), "received share destination should exist");
          expect(std::filesystem::exists(std::filesystem::path(received->destination_path) / "nested" / "notes.txt"),
                 "received share should contain nested copied file");
          expect(received->copied_file_count == folder_manifest->file_count, "received file count should match manifest");
          expect(received->copied_bytes == folder_manifest->total_bytes, "received bytes should match manifest");
        }

        const auto bundle_path = temp_root / "exported-share.tar.gz";
        const auto exported = hyprshare::core::export_staged_file_share(*staged, bundle_path, runner);
        expect(exported.has_value(), "staged share should export into a bundle");
        if (exported.has_value()) {
          expect(std::filesystem::exists(*exported), "exported bundle should exist");

          constexpr std::uint16_t serve_port = 28448;
          std::expected<std::uintmax_t, std::string> served = std::unexpected(std::string("not started"));
          std::thread server([&]() {
            served = hyprshare::core::serve_staged_file_share(*staged, "127.0.0.1", serve_port, stage_paths, runner);
          });
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          const auto probe = hyprshare::core::probe_remote_file_share("localhost", serve_port);
          const auto offer = hyprshare::core::inspect_remote_file_share("localhost", serve_port);
          const auto fetched = hyprshare::core::fetch_staged_file_share("localhost", serve_port, stage_paths, runner);
          server.join();

          expect(probe.has_value(), "remote probe should confirm TCP reachability without consuming the transfer");
          expect(served.has_value(), "served staged share should complete after one client fetch");
          expect(offer.has_value(), "remote inspect should read the offered share metadata");
          expect(fetched.has_value(), "loopback fetch should import a staged share");
          if (!served.has_value()) {
            std::cerr << "served error: " << served.error() << "\n";
          }
          if (!offer.has_value()) {
            std::cerr << "offer error: " << offer.error() << "\n";
          }
          if (!fetched.has_value()) {
            std::cerr << "fetched error: " << fetched.error() << "\n";
          }
          if (offer.has_value()) {
            expect(offer->session_id == staged->session_id, "remote offer should preserve the staged session id");
            expect(offer->manifest.share_name == staged->manifest.share_name, "remote offer should preserve share name");
            expect(offer->manifest.file_count == staged->manifest.file_count, "remote offer should preserve file count");
            expect(offer->bundle_size > 0, "remote offer should declare a non-zero bundle size");
          }
          if (served.has_value() && fetched.has_value()) {
            expect(*served > 0, "served transfer should report sent bytes");
            expect(fetched->manifest.share_name == staged->manifest.share_name, "fetched staged share should preserve share name");
            expect(!fetched->checksum_path.empty(), "fetched staged share should preserve checksum file");
            const auto fetched_verified = hyprshare::core::verify_staged_file_share(*fetched, runner);
            expect(fetched_verified.has_value(), "fetched staged share should verify successfully");
          }

          constexpr std::uint16_t token_port = 28449;
          const std::string access_token = "test-token";
          std::expected<std::uintmax_t, std::string> token_served = std::unexpected(std::string("not started"));
          std::thread token_server([&]() {
            token_served =
              hyprshare::core::serve_staged_file_share(*staged, "127.0.0.1", token_port, stage_paths, runner, access_token);
          });
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          const auto rejected_offer = hyprshare::core::inspect_remote_file_share("127.0.0.1", token_port);
          const auto token_offer = hyprshare::core::inspect_remote_file_share("127.0.0.1", token_port, access_token);
          const auto token_fetched = hyprshare::core::fetch_staged_file_share("127.0.0.1", token_port, stage_paths, runner, access_token);
          token_server.join();

          expect(!rejected_offer.has_value(), "inspect without token should fail when access token is required");
          expect(token_served.has_value(), "token-protected served share should complete after one authorized fetch");
          expect(token_offer.has_value(), "inspect with token should succeed");
          expect(token_fetched.has_value(), "fetch with token should succeed");
          if (token_offer.has_value()) {
            expect(token_offer->auth_required, "token-protected offer should declare auth as required");
            expect(token_offer->auth_expires_at_unix == 0, "token-protected offer without ttl should not declare an expiry");
          }
          if (token_fetched.has_value()) {
            const auto token_fetched_verified = hyprshare::core::verify_staged_file_share(*token_fetched, runner);
            expect(token_fetched_verified.has_value(), "token-protected fetched share should verify successfully");
          }

          hyprshare::platform::Paths receiver_paths = paths;
          receiver_paths.state_dir = temp_root / "receiver-state";
          const auto receiver_identity = hyprshare::core::load_or_create_device_identity(receiver_paths);
          expect(receiver_identity.has_value(), "receiver identity should be creatable for trusted-peer transport");
          if (receiver_identity.has_value()) {
            const auto trusted_receiver = hyprshare::core::add_trusted_peer(
              receiver_identity->device_id,
              receiver_identity->device_label,
              stage_paths);
            expect(trusted_receiver.has_value(), "sender should be able to trust the receiver identity");

            constexpr std::uint16_t trusted_peer_port = 28451;
            std::expected<std::uintmax_t, std::string> trusted_peer_served = std::unexpected(std::string("not started"));
            std::thread trusted_peer_server([&]() {
              trusted_peer_served = hyprshare::core::serve_staged_file_share(
                *staged,
                "127.0.0.1",
                trusted_peer_port,
                stage_paths,
                runner,
                {},
                0,
                receiver_identity->device_id);
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const auto anonymous_offer = hyprshare::core::inspect_remote_file_share("127.0.0.1", trusted_peer_port);
            const auto wrong_peer_offer =
              hyprshare::core::inspect_remote_file_share("127.0.0.1", trusted_peer_port, {}, "wrong-peer");
            const auto trusted_offer = hyprshare::core::inspect_remote_file_share(
              "127.0.0.1",
              trusted_peer_port,
              {},
              receiver_identity->device_id);
            const auto trusted_fetched = hyprshare::core::fetch_staged_file_share(
              "127.0.0.1",
              trusted_peer_port,
              receiver_paths,
              runner,
              {},
              receiver_identity->device_id);
            trusted_peer_server.join();

            expect(!anonymous_offer.has_value(), "inspect without a declared peer should fail when a trusted peer is required");
            expect(!wrong_peer_offer.has_value(), "inspect with the wrong peer identity should fail");
            expect(trusted_offer.has_value(), "inspect with the trusted peer identity should succeed");
            expect(trusted_peer_served.has_value(), "trusted-peer served share should complete after an authorized fetch");
            expect(trusted_fetched.has_value(), "fetch with the trusted peer identity should succeed");
            if (trusted_offer.has_value()) {
              expect(trusted_offer->allowed_peer_id == receiver_identity->device_id,
                     "trusted offer should declare the allowed peer id");
              expect(!trusted_offer->sender_device_id.empty(), "trusted offer should expose the sender device id");
            }
            if (trusted_fetched.has_value()) {
              const auto trusted_fetched_verified = hyprshare::core::verify_staged_file_share(*trusted_fetched, runner);
              expect(trusted_fetched_verified.has_value(), "trusted-peer fetched share should verify successfully");
            }
          }

          constexpr std::uint16_t expiring_token_port = 28450;
          std::expected<std::uintmax_t, std::string> expiring_served = std::unexpected(std::string("not started"));
          std::thread expiring_server([&]() {
            expiring_served = hyprshare::core::serve_staged_file_share(
              *staged,
              "127.0.0.1",
              expiring_token_port,
              stage_paths,
              runner,
              access_token,
              1);
          });
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          const auto expiring_offer = hyprshare::core::inspect_remote_file_share("127.0.0.1", expiring_token_port, access_token);
          std::this_thread::sleep_for(std::chrono::milliseconds(1200));
          const auto expired_offer = hyprshare::core::inspect_remote_file_share("127.0.0.1", expiring_token_port, access_token);
          expiring_server.join();

          expect(expiring_offer.has_value(), "inspect before token expiry should succeed");
          if (expiring_offer.has_value()) {
            expect(expiring_offer->auth_required, "expiring token offer should declare auth as required");
            expect(expiring_offer->auth_expires_at_unix > 0, "expiring token offer should declare an expiry timestamp");
          }
          expect(!expired_offer.has_value(), "inspect after token expiry should fail");
          expect(!expiring_served.has_value(), "expiring token server should exit without serving after expiry");

          constexpr std::uint16_t serve_timeout_port = 28454;
          const auto serve_timed_out = hyprshare::core::serve_staged_file_share(
            *staged,
            "127.0.0.1",
            serve_timeout_port,
            stage_paths,
            runner,
            {},
            0,
            {},
            1);
          expect(!serve_timed_out.has_value(), "session serve should time out when no transfer client connects");
          if (!serve_timed_out.has_value()) {
            expect(serve_timed_out.error().find("timed out") != std::string::npos,
                   "session serve timeout error should explain the timeout");
          }

          std::filesystem::remove_all(stage_paths.state_dir / "shares");
          const auto imported = hyprshare::core::import_staged_file_share(*exported, stage_paths, runner);
          expect(imported.has_value(), "bundle should import back into a staged share");
          if (imported.has_value()) {
            expect(imported->session_id != staged->session_id, "imported session should get a fresh session id");
            expect(imported->manifest.share_name == staged->manifest.share_name, "imported session should preserve share name");
            expect(imported->manifest.file_count == staged->manifest.file_count, "imported session should preserve file count");
            expect(!imported->checksum_path.empty(), "imported session should preserve checksum file");

            const auto imported_verified = hyprshare::core::verify_staged_file_share(*imported, runner);
            expect(imported_verified.has_value(), "imported staged share should verify successfully");

            const auto imported_received = hyprshare::core::receive_staged_file_share(*imported, temp_root / "inbox");
            expect(imported_received.has_value(), "imported staged share should still be receivable");
            if (imported_received.has_value()) {
              expect(std::filesystem::exists(std::filesystem::path(imported_received->destination_path) / "nested" / "notes.txt"),
                     "imported received share should contain nested copied file");
            }

            {
              std::ofstream tampered(std::filesystem::path(imported->payload_path) / "root.txt", std::ios::trunc);
              tampered << "tampered";
            }
            expect(std::filesystem::file_size(std::filesystem::path(imported->payload_path) / "root.txt") == 8,
                   "tampered payload file should be rewritten before verification");
            const auto tampered_verification = hyprshare::core::verify_staged_file_share(*imported, runner);
            expect(!tampered_verification.has_value(), "tampering should make staged share verification fail");
          }
        }
      }
    }
  }

  // A client that cannot be served does not take the inbox down.
  {
    const auto tls_dir = temp_root / "tls-inbox";
    std::filesystem::create_directories(tls_dir);
    const auto cert_path = temp_root / "tls-cert.pem";
    const auto key_path = temp_root / "tls-key.pem";
    const auto generated = hyprshare::tls::generate_self_signed_cert(
      cert_path, key_path, "inbox-device");
    expect(generated.has_value(), "the certificate should be generated");

    const auto payload = temp_root / "tls-payload.txt";
    {
      std::ofstream out(payload);
      out << "the content of the encrypted upload";
    }

    constexpr std::uint16_t tls_port = 28454;
    std::expected<std::vector<hyprshare::model::InboxReceivedFile>, std::string> tls_received =
      std::unexpected(std::string("not started"));
    std::thread tls_server([&]() {
      hyprshare::tls::TlsTransportConfig tls_config;
      tls_config.enabled = true;
      tls_config.cert_path = cert_path;
      tls_config.key_path = key_path;
      tls_received = hyprshare::core::serve_inbox(
        "127.0.0.1", tls_port, tls_dir, 2, "", 10, 0, &runner, tls_config);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // A client that speaks plain text cannot be served, but the inbox has to
    // stay up for the next client.
    const auto plain = hyprshare::core::put_inbox_file(
      "127.0.0.1", tls_port, payload, "plain.txt", "", &runner);
    expect(!plain.has_value(), "a plain text client should not be served by a TLS inbox");

    hyprshare::tls::TlsTransportConfig client_tls;
    client_tls.enabled = true;
    client_tls.ca_path = cert_path;
    client_tls.verify_peer = true;

    const auto secure = hyprshare::core::put_inbox_file(
      "127.0.0.1", tls_port, payload, "secure.txt", "", &runner, client_tls);
    expect(secure.has_value(),
      "the inbox should still serve a TLS client: " + (secure.has_value() ? "" : secure.error()));

    if (tls_server.joinable()) {
      tls_server.join();
    }
  }

  // An upload reports its progress.
  {
    const auto progress_dir = temp_root / "progress-inbox";
    std::filesystem::create_directories(progress_dir);
    const auto payload = temp_root / "progress.bin";
    {
      std::ofstream out(payload, std::ios::binary);
      out << std::string(200000, 'p');
    }

    constexpr std::uint16_t progress_port = 28453;
    std::expected<std::vector<hyprshare::model::InboxReceivedFile>, std::string> progress_received =
      std::unexpected(std::string("not started"));
    std::thread progress_server([&]() {
      progress_received = hyprshare::core::serve_inbox(
        "127.0.0.1", progress_port, progress_dir, 1, "", 5, 0, &runner);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::uint64_t last_sent = 0;
    std::uint64_t reported_total = 0;
    std::size_t reports = 0;
    const hyprshare::core::UploadProgressCallback progress =
      [&](const std::uint64_t sent, const std::uint64_t total) {
        last_sent = sent;
        reported_total = total;
        ++reports;
      };

    const auto uploaded = hyprshare::core::put_inbox_file(
      "127.0.0.1", progress_port, payload, "progress.bin", "", &runner, {}, &progress);
    expect(uploaded.has_value(), "the file should be uploaded");
    expect(reports > 0, "the upload should report its progress");
    expect(last_sent == reported_total, "the progress should end at the total size");
    expect(reported_total == 200000, "the total should be the size of the file");

    if (progress_server.joinable()) {
      progress_server.join();
    }
  }

  // The inbox listing tells a peer which files it holds.
  {
    std::cout << "Testing inbox listing...\n";

    const auto listed_dir = temp_root / "inbox-listed";
    std::filesystem::create_directories(listed_dir);
    const auto listed_file = temp_root / "listed.txt";
    {
      std::ofstream out(listed_file);
      out << "listed content";
    }

    constexpr std::uint16_t list_port = 28461;
    std::expected<std::vector<hyprshare::model::InboxReceivedFile>, std::string> list_received =
      std::unexpected(std::string("not started"));
    std::thread list_server([&]() {
      list_received = hyprshare::core::serve_inbox(
        "127.0.0.1", list_port, listed_dir, 2, "list-token", 5, 0, &runner);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Before anything was uploaded the listing is empty, and asking for it
    // does not count as an upload.
    {
      const auto listing = hyprshare::core::list_inbox_files("127.0.0.1", list_port, "list-token");
      expect(listing.has_value(),
        "the inbox should answer a listing request: " + (listing.has_value() ? "" : listing.error()));
      if (listing.has_value()) {
        expect(listing->empty(), "a fresh inbox should list no files");
      }
    }

    // A listing without the right token is refused.
    {
      const auto refused = hyprshare::core::list_inbox_files("127.0.0.1", list_port, "wrong-token");
      expect(!refused.has_value(), "a listing without the right token should be refused");
    }

    const auto listed_upload = hyprshare::core::put_inbox_file(
      "127.0.0.1", list_port, listed_file, "listed.txt", "list-token");
    expect(listed_upload.has_value(), "the file should be uploaded");

    {
      const auto listing = hyprshare::core::list_inbox_files("127.0.0.1", list_port, "list-token");
      expect(listing.has_value(), "the inbox should answer a listing request");
      if (listing.has_value()) {
        expect(listing->size() == 1, "the uploaded file should be listed");
        if (!listing->empty()) {
          expect(listing->front().name == "listed.txt", "the listed name should be the remote name");
          expect(listing->front().bytes > 0, "the listed size should not be empty");
        }
      }
    }

    // The second upload lets the server finish.
    const auto second_listed = hyprshare::core::put_inbox_file(
      "127.0.0.1", list_port, listed_file, "second.txt", "list-token");
    expect(second_listed.has_value(), "the second file should be uploaded");
    if (list_server.joinable()) {
      list_server.join();
    }
  }

  std::filesystem::remove_all(temp_root);
  return test_support::report("core");
}
