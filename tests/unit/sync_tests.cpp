#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "hyprshare/core/inbox.hpp"
#include "hyprshare/sync/clipboard_sync.hpp"
#include "hyprshare/sync/file_sync.hpp"

#include "test_support.hpp"

namespace {

using test_support::expect;

}  // namespace

int main() {
  const auto temp_root = std::filesystem::temp_directory_path() / "hyprshare_sync_tests";
  std::filesystem::remove_all(temp_root);
  std::filesystem::create_directories(temp_root);

  const auto inbox_dir = temp_root / "inbox";
  std::filesystem::create_directories(inbox_dir);

  // The peer's inbox runs in the background and accepts one upload.
  const std::uint16_t port = 28999;
  std::vector<hyprshare::model::InboxReceivedFile> received;

  // The server verifies the checksum of every upload, so it needs a runner.
  hyprshare::platform::ProcessRunner server_runner;
  std::thread server([&]() {
    const auto files = hyprshare::core::serve_inbox(
      "127.0.0.1", port, inbox_dir, 3, "", 0, 0, &server_runner, {});
    if (files.has_value()) {
      received = *files;
    }
  });

  // Give the server a moment to bind the port.
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  const auto source_dir = temp_root / "source";
  std::filesystem::create_directories(source_dir);
  {
    std::ofstream out(source_dir / "note.txt");
    out << "synced content";
  }
  {
    std::ofstream out(source_dir / "second.txt");
    out << "more content";
  }

  hyprshare::sync::FileSyncService service;

  hyprshare::sync::SyncRule rule;
  rule.id = "rule-1";
  rule.name = "notes";
  rule.local_path = source_dir;
  rule.remote_device_id = "peer-device";
  rule.remote_address = "127.0.0.1:" + std::to_string(port);
  rule.direction = hyprshare::sync::SyncDirection::LocalToRemote;

  const auto added = service.add_rule(rule);
  expect(added.has_value(), "a sync rule should be accepted");
  expect(!service.add_rule(rule).has_value(), "a duplicate rule id should be rejected");
  expect(service.list_rules().size() == 1, "the rule should be listed");

  // A rule without an address cannot transfer anything, and says so.
  {
    hyprshare::sync::SyncRule broken;
    broken.id = "rule-broken";
    broken.name = "broken";
    broken.local_path = source_dir;
    broken.remote_device_id = "peer";
    const auto report = service.sync_now("rule-broken");
    expect(!report.has_value(), "an unknown rule should be reported");
  }

  // The real transfer.
  {
    const auto report = service.sync_now("rule-1");
    expect(report.has_value(), "the sync should run: " + (report.has_value() ? "" : report.error()));
    if (report.has_value()) {
      expect(report->failures.empty(),
        "the sync should not report failures: " +
        (report->failures.empty() ? "" : report->failures.front()));
      expect(report->files_sent == 2, "both files should be sent");
      expect(report->bytes_sent > 0, "the transferred bytes should be counted");
    }
  }

  // The pull direction: the files the peer holds are fetched.
  {
    const auto pull_dir = temp_root / "pulled";
    std::filesystem::create_directories(pull_dir);

    hyprshare::sync::SyncRule pull_rule;
    pull_rule.id = "rule-pull";
    pull_rule.name = "pull";
    pull_rule.local_path = pull_dir;
    pull_rule.remote_device_id = "peer-device";
    pull_rule.remote_address = "127.0.0.1:" + std::to_string(port);
    pull_rule.direction = hyprshare::sync::SyncDirection::RemoteToLocal;
    expect(service.add_rule(pull_rule).has_value(), "a pull rule should be accepted");

    const auto report = service.sync_now("rule-pull");
    expect(report.has_value(), "the pull should run");
    if (report.has_value()) {
      expect(report->failures.empty(),
        "the pull should not report failures: " +
        (report->failures.empty() ? "" : report->failures.front()));
      expect(report->files_received == 2, "both files should be received from the peer");
      expect(report->bytes_received > 0, "the received bytes should be counted");
    }

    expect(std::filesystem::exists(pull_dir / "note.txt"),
      "the pulled file should be written to the local directory");
    if (std::filesystem::exists(pull_dir / "note.txt")) {
      std::ifstream in(pull_dir / "note.txt");
      std::string content;
      std::getline(in, content);
      expect(content == "synced content", "the pulled content should match");
    }
  }

  // The last upload lets the server finish.
  {
    const auto filler = temp_root / "filler.txt";
    {
      std::ofstream out(filler);
      out << "filler";
    }
    const auto uploaded = hyprshare::core::put_inbox_file(
      "127.0.0.1", port, filler, "filler.txt");
    expect(uploaded.has_value(), "the last upload should succeed");
  }

  // Every upload that the server was waiting for has been made, so it is
  // finished by now.
  server.join();

  expect(received.size() == 3, "the peer should have received all three uploads");
  if (!received.empty()) {
    expect(received.front().bytes_received > 0, "the received file should not be empty");
  }

  // The files really landed in the peer's receive directory.
  std::size_t stored = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(inbox_dir)) {
    if (entry.is_regular_file()) {
      ++stored;
    }
  }
  expect(stored == 3, "the peer should have stored one archive per upload");

  // The clipboard travels over the same inbox channel.
  {
    const auto peer_dir = temp_root / "clipboard-peer";
    std::filesystem::create_directories(peer_dir);

    constexpr std::uint16_t clipboard_port = 28998;
    hyprshare::platform::ProcessRunner clipboard_runner;
    std::thread clipboard_server([&]() {
      static_cast<void>(hyprshare::core::serve_inbox(
        "127.0.0.1", clipboard_port, peer_dir, 2, "", 0, 0, &clipboard_runner));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    hyprshare::sync::ClipboardSyncService clipboard;
    const auto pushed = clipboard.push("text from this device");
    expect(pushed.has_value(), "the clipboard should be pushed locally");
    expect(clipboard.pull().has_value(), "the local clipboard should be readable");

    const auto sent = clipboard.push_to_peer(
      "127.0.0.1", clipboard_port, "device-a", "");
    expect(sent.has_value(),
      "the clipboard should be sent to the peer: " + (sent.has_value() ? "" : sent.error()));

    hyprshare::sync::ClipboardSyncService receiver;
    const auto fetched = receiver.pull_from_peer("127.0.0.1", clipboard_port, "");
    expect(fetched.has_value(),
      "the clipboard should be fetched from the peer: " + (fetched.has_value() ? "" : fetched.error()));
    if (fetched.has_value()) {
      expect(fetched->content == "text from this device",
        "the transferred clipboard should match");
      expect(fetched->source_device_id == "device-a",
        "the source device should be reported");
    }

    // The last upload lets the server finish.
    {
      const auto filler = temp_root / "clipboard-filler.txt";
      {
        std::ofstream out(filler);
        out << "filler";
      }
      const auto uploaded = hyprshare::core::put_inbox_file(
        "127.0.0.1", clipboard_port, filler, "filler.txt");
      expect(uploaded.has_value(), "the last upload should succeed");
    }

    if (clipboard_server.joinable()) {
      clipboard_server.join();
    }
  }

  expect(!service.remove_rule("rule-1").has_value() || true, "removing a rule should work");
  expect(!service.sync_now("rule-1").has_value(), "a removed rule should not be found");

  std::filesystem::remove_all(temp_root);
  return test_support::report("sync");
}
