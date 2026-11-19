#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>
#include <thread>

#include "hyprshare/backup/backup_manager.hpp"
#include "hyprshare/diagnostics/network_diagnostics.hpp"
#include "hyprshare/mobile/mobile_transfer.hpp"
#include "hyprshare/perf/performance.hpp"
#include "hyprshare/profile/profile_manager.hpp"
#include "hyprshare/remote/remote_control.hpp"
#include "hyprshare/security/security_auditor.hpp"
#include "hyprshare/shell/interactive_shell.hpp"
#include "hyprshare/web/api_server.hpp"

#include "test_support.hpp"

namespace {

using test_support::expect;

}  // namespace

int main() {
  const auto temp_root = std::filesystem::temp_directory_path() / "hyprshare_service_tests";
  std::filesystem::remove_all(temp_root);
  std::filesystem::create_directories(temp_root);

  // Mobile transfer tests
  {
    std::cout << "Testing mobile transfer...\n";

    hyprshare::mobile::MobileTransferServer server(0);
    expect(!server.is_running(), "mobile server should not be running initially");

    auto start_result = server.start();
    expect(start_result.has_value(), "mobile server should start");
    if (start_result.has_value()) {
      expect(server.is_running(), "mobile server should be running after start");
      server.stop();
      expect(!server.is_running(), "mobile server should stop after stop()");
    }
  }

  // The mobile protocol itself: a client discovers the service, authenticates,
  // and offers a file.
  {
    std::cout << "Testing the mobile protocol...\n";

    // The server writes the received files into the home directory, so the
    // test uses a throwaway one.
    const auto home = temp_root / "mobile-home";
    std::filesystem::create_directories(home);
    const char* original_home = getenv("HOME");
    const std::string saved_home = original_home != nullptr ? original_home : "";
    setenv("HOME", home.c_str(), 1);

    hyprshare::mobile::MobileTransferServer server(28571);
    const auto started = server.start();
    expect(started.has_value(), "the mobile server should start");

    if (started.has_value()) {
      const int client = socket(AF_INET, SOCK_STREAM, 0);
      expect(client >= 0, "the test client socket should be created");

      if (client >= 0) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(28571);
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        expect(connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
          "the test client should connect");

        // The discovery request.
        const std::string discover = "{\"type\":\"discover\",\"payload\":{}}\n";
        expect(send(client, discover.data(), discover.size(), MSG_NOSIGNAL) > 0,
          "the discovery request should be sent");

        std::string response;
        char buffer[4096];
        const auto received = recv(client, buffer, sizeof(buffer) - 1, 0);
        expect(received > 0, "the server should answer the discovery request");
        if (received > 0) {
          buffer[received] = '\0';
          response = buffer;
          expect(response.find("discover_response") != std::string::npos,
            "the server should answer with a discovery response: " + response);
          expect(response.find("file_transfer") != std::string::npos,
            "the discovery response should list the capabilities: " + response);
        }

        // The authentication request.
        const std::string auth =
          "{\"type\":\"auth_request\",\"payload\":{\"device_id\":\"phone-1\","
          "\"device_name\":\"Test Phone\"}}\n";
        expect(send(client, auth.data(), auth.size(), MSG_NOSIGNAL) > 0,
          "the authentication request should be sent");

        const auto auth_received = recv(client, buffer, sizeof(buffer) - 1, 0);
        expect(auth_received > 0, "the server should answer the authentication request");
        if (auth_received > 0) {
          buffer[auth_received] = '\0';
          response = buffer;
          expect(response.find("auth_response") != std::string::npos,
            "the server should answer with an authentication response: " + response);
        }

        // The file offer, followed by the bytes of the file.
        const std::string content = "the content of the transferred file";
        const std::string offer =
          "{\"type\":\"file_offer\",\"payload\":{\"name\":\"photo.jpg\",\"size\":" +
          std::to_string(content.size()) + "}}\n";
        expect(send(client, offer.data(), offer.size(), MSG_NOSIGNAL) > 0,
          "the file offer should be sent");
        expect(send(client, content.data(), content.size(), MSG_NOSIGNAL) > 0,
          "the file content should be sent");

        const auto accept_received = recv(client, buffer, sizeof(buffer) - 1, 0);
        expect(accept_received > 0, "the server should accept the file");
        if (accept_received > 0) {
          buffer[accept_received] = '\0';
          response = buffer;
          expect(response.find("file_accept") != std::string::npos,
            "the server should accept the file: " + response);
        }

        // Give the server a moment to write the file.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        const auto received_file = home / ".local/share/hyprshare/inbox/photo.jpg";
        expect(std::filesystem::exists(received_file),
          "the offered file should be written to the inbox of the device");
        if (std::filesystem::exists(received_file)) {
          std::ifstream in(received_file, std::ios::binary);
          std::string received_content((std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>());
          expect(received_content == content, "the transferred content should match");
        }

        close(client);
      }

      server.stop();
    }

    if (original_home != nullptr) {
      setenv("HOME", saved_home.c_str(), 1);
    } else {
      unsetenv("HOME");
    }
  }

  // The remote control protocol: a client sends an input event and the
  // server reports it through the callback.
  {
    std::cout << "Testing the remote control protocol...\n";

    hyprshare::remote::RemoteControlServer server(28572);
    const auto started = server.start();
    expect(started.has_value(), "the remote control server should start");

    if (started.has_value()) {
      std::mutex event_mutex;
      std::vector<hyprshare::remote::InputEvent> events;
      server.set_input_callback([&](const hyprshare::remote::InputEvent& event) {
        std::lock_guard<std::mutex> lock(event_mutex);
        events.push_back(event);
      });

      const int client = socket(AF_INET, SOCK_STREAM, 0);
      expect(client >= 0, "the test client socket should be created");

      if (client >= 0) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(28572);
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        expect(connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
          "the test client should connect");

        const std::string event =
          "{\"type\":\"input_event\",\"payload\":{\"event_type\":\"touch\","
          "\"x\":120,\"y\":340}}\n";
        expect(send(client, event.data(), event.size(), MSG_NOSIGNAL) > 0,
          "the input event should be sent");

        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        std::lock_guard<std::mutex> lock(event_mutex);
        expect(events.size() == 1, "the server should report one input event");
        if (!events.empty()) {
          expect(events.front().type == hyprshare::remote::InputEventType::Touch,
            "the event should be a touch");
          expect(events.front().x == 120, "the event should carry the x coordinate");
          expect(events.front().y == 340, "the event should carry the y coordinate");
        }

        close(client);
      }

      server.stop();
    }
  }

  // Remote control tests
  {
    std::cout << "Testing remote control...\n";

    hyprshare::remote::RemoteControlServer server(0);
    expect(!server.is_running(), "remote server should not be running initially");

    auto start_result = server.start();
    expect(start_result.has_value(), "remote server should start");
    if (start_result.has_value()) {
      expect(server.is_running(), "remote server should be running after start");
      server.stop();
      expect(!server.is_running(), "remote server should stop after stop()");
    }
  }

  // Performance tests
  {
    std::cout << "Testing performance...\n";

    hyprshare::perf::BufferPool pool(4096, 8);
    expect(pool.buffer_size() == 4096, "buffer pool should have correct buffer size");
    expect(pool.total() == 8, "buffer pool should have correct total buffers");
    expect(pool.available() == 8, "buffer pool should start with all buffers available");

    auto buffer = pool.acquire();
    expect(buffer != nullptr, "buffer pool should acquire a buffer");
    expect(pool.available() == 7, "buffer pool should have one less available buffer");

    pool.release(buffer);
    expect(pool.available() == 8, "buffer pool should have all buffers available after release");

    hyprshare::perf::ProgressTracker tracker;
    tracker.start(1000);
    expect(!tracker.is_complete(), "progress tracker should not be complete initially");

    tracker.update(500);
    auto progress = tracker.current();
    expect(progress.percentage == 50.0, "progress tracker should show 50%");

    tracker.finish();
    expect(tracker.is_complete(), "progress tracker should be complete after finish");

    hyprshare::perf::RateLimiter limiter(1024);
    expect(limiter.rate() == 1024, "rate limiter should have correct rate");
    expect(limiter.try_acquire(512), "rate limiter should allow acquiring half the rate");
    expect(!limiter.try_acquire(1024), "rate limiter should reject acquiring more than rate");
  }

  // Web API server tests
  {
    std::cout << "Testing web API server...\n";

    hyprshare::web::ApiServer server(0);
    expect(!server.is_running(), "API server should not be running initially");

    auto start_result = server.start();
    expect(start_result.has_value(), "API server should start");
    if (start_result.has_value()) {
      expect(server.is_running(), "API server should be running after start");
      server.stop();
      expect(!server.is_running(), "API server should stop after stop()");
    }
  }

  // Interactive shell tests
  {
    std::cout << "Testing interactive shell...\n";

    hyprshare::shell::InteractiveShell shell;
    shell.register_command({"test", "Test command", [](const std::vector<std::string>&) { return 0; }});

    auto result = shell.execute("test");
    expect(result.has_value(), "shell should execute registered command");

    auto unknown_result = shell.execute("unknown");
    expect(!unknown_result.has_value(), "shell should fail on unknown command");

    auto help_result = shell.execute("help");
    expect(help_result.has_value(), "shell should handle help command");
  }

  // Security auditor tests
  {
    std::cout << "Testing security auditor...\n";

    hyprshare::security::SecurityAuditor auditor;
    auto report = auditor.run_audit();
    expect(report.has_value(), "security audit should complete");
    if (report.has_value()) {
      expect(report->checks.size() > 0, "security audit should have checks");
      expect(report->score >= 0, "security score should be non-negative");
    }
  }

  // Backup manager tests
  {
    std::cout << "Testing backup manager...\n";

    // The backup manager resolves the state directory from $HOME, so point it
    // at a throwaway home instead of the real one.
    const auto test_home = temp_root / "home";
    std::filesystem::create_directories(test_home / ".local/state/hyprshare/shares");
    std::filesystem::create_directories(test_home / ".config/hyprshare");
    {
      std::ofstream state_file(test_home / ".local/state/hyprshare/shares/data.txt");
      state_file << "state\n";
    }

    const char* original_home = getenv("HOME");
    const std::string saved_home = original_home != nullptr ? original_home : "";
    setenv("HOME", test_home.c_str(), 1);

    hyprshare::backup::BackupManager manager;
    const auto created = manager.create_backup("unit test backup");
    expect(created.has_value(), "backup manager should create a backup");

    if (created.has_value()) {
      const auto backups = manager.list_backups();
      expect(backups.size() == 1, "a freshly created backup should be the only one listed");

      // The backup directory lives inside the state directory. Copying the
      // state directory must skip the backup directory, otherwise every backup
      // nests the previous ones and the state directory grows without bound.
      const auto nested = test_home / ".local/state/hyprshare/backups" / *created / "state/backups";
      expect(!std::filesystem::exists(nested),
        "a backup should not contain a copy of the backup directory");

      const auto inspected = manager.inspect_backup(*created);
      expect(inspected.has_value(), "a created backup should be inspectable");

      const auto removed = manager.delete_backup(*created);
      expect(removed.has_value(), "backup manager should delete a backup");
      expect(manager.list_backups().empty(), "a deleted backup should disappear from the listing");
    }

    if (original_home != nullptr) {
      setenv("HOME", saved_home.c_str(), 1);
    } else {
      unsetenv("HOME");
    }
  }

  // Profile tests
  {
    std::cout << "Testing profile manager...\n";

    // The manager keeps its profiles below the home directory.
    const auto home = temp_root / "profile-home";
    std::filesystem::create_directories(home);
    const char* original_home = getenv("HOME");
    const std::string saved_home = original_home != nullptr ? original_home : "";
    setenv("HOME", home.c_str(), 1);

    hyprshare::profile::ProfileManager manager;

    const auto created = manager.create_profile("work", "me@example.com");
    expect(created.has_value(), "a profile should be created");
    expect(manager.list_profiles().size() == 1, "the profile should be listed");

    const auto profiles = manager.list_profiles();
    if (!profiles.empty()) {
      expect(profiles.front().name == "work", "the profile should keep its name");
      expect(profiles.front().email == "me@example.com", "the profile should keep its address");
      expect(profiles.front().created_at > 0, "the profile should know when it was created");
      expect(profiles.front().active, "the first profile should be the active one");

      // The profile can be found by its identifier.
      const auto loaded = manager.get_profile(profiles.front().id);
      expect(loaded.has_value(), "the profile should be found by its identifier");
      if (loaded.has_value()) {
        expect(loaded->name == "work", "the loaded profile should keep its name");
      }
    }

    // The active profile is the one that was switched to.
    const auto active = manager.active_profile();
    expect(active.has_value(), "there should be an active profile");

    // Two profiles created in a row must not share a directory.
    const auto second = manager.create_profile("home", "");
    expect(second.has_value(), "a second profile should be created");
    expect(manager.list_profiles().size() == 2, "both profiles should be listed");

    // The manager works with the identifier of a profile.
    std::string home_id;
    for (const auto& profile : manager.list_profiles()) {
      if (profile.name == "home") {
        home_id = profile.id;
      }
    }
    expect(!home_id.empty(), "the second profile should have an identifier");

    const auto switched = manager.switch_profile(home_id);
    expect(switched.has_value(), "the profile should be switched");
    const auto after_switch = manager.active_profile();
    expect(after_switch.has_value(), "there should be an active profile");
    if (after_switch.has_value()) {
      expect(after_switch->name == "home", "the switched profile should be the active one");
    }

    // The profile can be exported and imported again. The directory is named
    // after the profile, so that the import keeps the identifier.
    const auto export_path = home / home_id;
    const auto exported = manager.export_profile(home_id, export_path);
    expect(exported.has_value(), "the profile should be exported");
    const auto imported = manager.import_profile(export_path);
    expect(imported.has_value(), "the profile should be imported");

    // And it can be deleted.
    const auto deleted = manager.delete_profile(home_id);
    expect(deleted.has_value(), "the profile should be deleted");
    expect(manager.list_profiles().size() == 1, "the deleted profile should be gone");

    if (original_home != nullptr) {
      setenv("HOME", saved_home.c_str(), 1);
    } else {
      unsetenv("HOME");
    }
  }

  // Network diagnostics tests
  {
    std::cout << "Testing network diagnostics...\n";

    hyprshare::diagnostics::NetworkDiagnosticsRunner net_runner;
    auto interfaces = net_runner.list_interfaces();
    expect(interfaces.has_value(), "network diagnostics should list interfaces");
    if (interfaces.has_value()) {
      expect(!interfaces->empty(), "network diagnostics should have interfaces");
    }
  }

  std::filesystem::remove_all(temp_root);
  return test_support::report("services");
}
