#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "hyprshare/core/inbox.hpp"
#include "hyprshare/daemon/daemon_state.hpp"
#include "hyprshare/daemon/ipc.hpp"

#include "test_support.hpp"

namespace {

using test_support::expect;

}  // namespace

int main() {
  // The request has to carry its arguments. They used to be dropped, which
  // left every command with its defaults.
  {
    hyprshare::daemon::IpcRequest request;
    request.command = "inbox.start_listener";
    request.args["host"] = "127.0.0.1";
    request.args["port"] = "28556";
    request.args["destination"] = "/tmp/receive dir";
    request.args["token"] = "a \"quoted\" token";

    const auto parsed = hyprshare::daemon::parse_request(
      hyprshare::daemon::serialize_request(request));
    expect(parsed.has_value(), "a serialized request should parse again");
    if (parsed.has_value()) {
      expect(parsed->command == "inbox.start_listener", "the command should survive");
      expect(parsed->args.size() == 4, "every argument should survive");
      expect(parsed->args.at("host") == "127.0.0.1", "the host should survive");
      expect(parsed->args.at("port") == "28556", "the port should survive");
      expect(parsed->args.at("destination") == "/tmp/receive dir",
        "a value with a space should survive");
      expect(parsed->args.at("token") == "a \"quoted\" token",
        "a value with quotes should survive");
    }
  }

  // The answer has to carry its result. It used to be skipped, which left the
  // caller without any answer.
  {
    hyprshare::daemon::IpcResponse response;
    response.ok = true;
    response.result["count"] = "2";
    response.result["listeners"] = "[{\"id\":1,\"host\":\"127.0.0.1\"}]";

    const auto parsed = hyprshare::daemon::parse_response(
      hyprshare::daemon::serialize_response(response));
    expect(parsed.has_value(), "a serialized response should parse again");
    if (parsed.has_value()) {
      expect(parsed->ok, "the status should survive");
      expect(parsed->result.size() == 2, "every result field should survive");
      expect(parsed->result.at("count") == "2", "the count should survive");
      expect(parsed->result.at("listeners").find("127.0.0.1") != std::string::npos,
        "a result with quotes should survive");
    }
  }

  // A request without arguments is still a request.
  {
    const auto parsed = hyprshare::daemon::parse_request(
      "{\"command\":\"daemon.ping\",\"args\":{}}");
    expect(parsed.has_value(), "a request without arguments should parse");
    if (parsed.has_value()) {
      expect(parsed->command == "daemon.ping", "the command should be read");
      expect(parsed->args.empty(), "there should be no arguments");
    }
  }

  // The listener of the daemon really receives a file.
  {
    const auto home = std::filesystem::temp_directory_path() / "hyprshare-daemon-tests";
    std::filesystem::remove_all(home);
    std::filesystem::create_directories(home);

    auto paths = hyprshare::platform::resolve_paths();
    paths.state_dir = home / "state";
    paths.config_dir = home / "config";
    std::filesystem::create_directories(paths.state_dir);
    std::filesystem::create_directories(paths.config_dir);

    const auto config = hyprshare::core::load_config(paths);
    expect(config.has_value(), "the configuration should load");
    if (!config.has_value()) {
      return test_support::report("daemon");
    }

    const auto backends = hyprshare::core::BackendRegistry::load(paths.data_dir / "backends");
    const auto adapters = hyprshare::core::AdapterRegistry::load(paths.data_dir / "compat" / "apps");
    expect(backends.has_value(), "the backends should load");
    expect(adapters.has_value(), "the adapters should load");
    if (!backends.has_value() || !adapters.has_value()) {
      return test_support::report("daemon");
    }

    hyprshare::daemon::DaemonState state(paths, *config, *backends, *adapters);

    const auto receive_dir = home / "recv";
    constexpr std::uint16_t port = 28583;
    const auto started = state.start_listener("127.0.0.1", port, receive_dir, 0, 0, 0, "", false);
    expect(started, "the listener should start");

    const auto listeners = state.list_listeners();
    expect(listeners.size() == 1, "the listener should be listed");
    if (!listeners.empty()) {
      expect(listeners.front().running, "the listener should be running");
      expect(listeners.front().port == port, "the listener should report its port");
    }

    // Wait for the listener to bind the port.
    bool bound = false;
    for (int attempt = 0; attempt < 40 && !bound; ++attempt) {
      const int probe = socket(AF_INET, SOCK_STREAM, 0);
      if (probe >= 0) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        bound = connect(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
        close(probe);
      }
      if (!bound) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
    expect(bound, "the listener should bind its port");

    if (bound) {
      const auto source = home / "payload.txt";
      {
        std::ofstream out(source);
        out << "the content of the daemon upload";
      }
      const auto uploaded = hyprshare::core::put_inbox_file(
        "127.0.0.1", port, source, "payload.txt");
      expect(uploaded.has_value(),
        "the file should be uploaded: " + (uploaded.has_value() ? "" : uploaded.error()));

      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      const auto received = receive_dir / "payload.txt";
      expect(std::filesystem::exists(received),
        "the daemon should have received the file");
      if (std::filesystem::exists(received)) {
        std::ifstream in(received, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in)),
          std::istreambuf_iterator<char>());
        expect(content == "the content of the daemon upload",
          "the received content should match");
      }
    }

    // The listener can be stopped again.
    const auto stopped = state.stop_listener(1);
    expect(stopped, "the listener should stop");
    const auto after = state.list_listeners();
    expect(!after.empty() && !after.front().running, "the listener should no longer run");

    state.shutdown();
    std::filesystem::remove_all(home);
  }

  return test_support::report("daemon");
}
