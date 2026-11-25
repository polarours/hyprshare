#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "hyprshare/http/http_client.hpp"

// Regression tests for the CLI dispatch layer.
//
// These tests drive the real `hyprshare` binary so they cover argument parsing
// and subcommand routing exactly as a user experiences it.
//
// CONTRACT: the table below lists every top-level command. When a command is
// added to the CLI, add it here as well -- the test asserts that the command
// appears in `hyprshare --help`, which keeps the help output from drifting
// away from the actual command surface.

namespace {

int failures = 0;

// Every invocation runs against a throwaway HOME so the tests never touch the
// real configuration or state directory.
std::string test_home;

void expect(const bool condition, const std::string& message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAILED: " << message << "\n";
  }
}

struct RunResult {
  std::string output;
  int exit_code = -1;
};

RunResult run_hyprshare(const std::string& binary, const std::string& args) {
  RunResult result;
  const std::string command = "HOME='" + test_home + "' '" + binary + "' " + args + " 2>&1";
  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    return result;
  }
  char buffer[4096];
  while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    result.output += buffer;
  }
  const int status = pclose(pipe);
  result.exit_code = (status != -1 && WIFEXITED(status)) ? WEXITSTATUS(status) : -1;
  return result;
}

struct CommandCase {
  std::string command;
  // Arguments used to reach a subcommand branch. Empty means the command has
  // no side-effect-free subcommand to probe.
  std::string probe;
};

// The first address of this machine that is not the loopback address.
std::string first_non_loopback_ipv4() {
  ifaddrs* interfaces = nullptr;
  if (getifaddrs(&interfaces) != 0) {
    return "";
  }

  std::string found;
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
    if (address.rfind("127.", 0) == 0) {
      continue;
    }
    found = address;
    break;
  }

  freeifaddrs(interfaces);
  return found;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: cli_dispatch_tests <path-to-hyprshare> [path-to-example-plugin.so]\n";
    return 2;
  }
  const std::string binary = argv[1];

  const std::filesystem::path temp_root =
    std::filesystem::temp_directory_path() / "hyprshare-cli-dispatch-tests";
  std::filesystem::remove_all(temp_root);
  std::filesystem::create_directories(temp_root);
  test_home = (temp_root / "home").string();
  std::filesystem::create_directories(test_home);

  const std::string share_file = (temp_root / "probe.txt").string();
  {
    std::ofstream out(share_file);
    out << "probe\n";
  }

  const std::string support_dir = (temp_root / "bundle").string();

  const std::vector<CommandCase> cases = {
    {"status", ""},
    {"doctor", ""},
    {"check", ""},
    {"support", "bundle --to " + support_dir},
    {"discovery", "status"},
    {"backend", "list"},
    {"compat", "show wemeet"},
    {"config", "show"},
    {"clipboard", "read"},
    {"inbox", "status"},
    {"peer", "list"},
    {"session", "list"},
    {"share", "file " + share_file},
    {"app", "launch wemeet --dry-run"},
    {"sync", "status"},
    {"clipboard-sync", "status"},
    {"record", "status"},
    {"mobile", "send"},
    {"remote", "status"},
    {"shell", ""},
    {"netdiag", "interfaces"},
    {"tls", "status"},
    {"batch", "run-commands true"},
    {"plugin", "list"},
    {"monitor", "status"},
    {"audit", "list"},
    {"backup", "list"},
    {"cloud", "status"},
    {"search", "files --path " + temp_root.string()},
    {"update", "status"},
    {"profile", "list"},
    {"profiling", "report"},
  };

  // Every registered command must be reachable and must not fall through to
  // the "unknown subcommand" path. Handlers compare the subcommand against
  // args[1]; comparing against args[0] (the command name) makes every
  // subcommand unreachable.
  //
  // Commands without a probe have no subcommand to dispatch, so the
  // unknown-subcommand check does not apply to them.
  for (const auto& test_case : cases) {
    const std::string args = test_case.probe.empty()
      ? test_case.command
      : test_case.command + " " + test_case.probe;
    const RunResult result = run_hyprshare(binary, args);

    expect(result.exit_code != -1,
      "command '" + args + "' terminated abnormally");
    expect(result.output.find("unknown command:") == std::string::npos,
      "command '" + args + "' should be registered");
    if (!test_case.probe.empty()) {
      expect(result.output.find("unknown " + test_case.command + " subcommand") == std::string::npos,
        "command '" + args + "' fell through to the unknown-subcommand path");
    }
  }

  // The top-level help must advertise every command, and per-command help must
  // resolve for each of them.
  const RunResult help = run_hyprshare(binary, "--help");
  expect(help.exit_code == 0, "`hyprshare --help` should exit successfully");
  expect(help.output.find("Usage:") != std::string::npos,
    "`hyprshare --help` should print a usage section");

  for (const auto& test_case : cases) {
    expect(help.output.find("hyprshare " + test_case.command) != std::string::npos,
      "top-level help should mention the '" + test_case.command + "' command");

    const RunResult command_help = run_hyprshare(binary, test_case.command + " --help");
    expect(command_help.exit_code == 0,
      "`hyprshare " + test_case.command + " --help` should exit successfully");
    expect(command_help.output.find("Usage:") != std::string::npos,
      "`hyprshare " + test_case.command + " --help` should print a usage section");
  }

  // Bare invocation prints help; an unknown command is reported as an error.
  const RunResult bare = run_hyprshare(binary, "");
  expect(bare.exit_code == 0, "bare `hyprshare` should print help and exit successfully");
  expect(bare.output.find("Usage:") != std::string::npos,
    "bare `hyprshare` should print a usage section");

  const RunResult unknown = run_hyprshare(binary, "definitely-not-a-command");
  expect(unknown.exit_code != 0, "an unknown command should exit with a failure code");
  expect(unknown.output.find("unknown command:") != std::string::npos,
    "an unknown command should be reported as such");

  // The web API is exercised end to end: the server is started and every
  // route is asked for.
  {
    const std::string web_port = "28933";
    const std::string web_log = temp_root.string() + "/web.log";

    const std::string start_command =
      "HOME='" + test_home + "' '" + binary + "' web serve --port " + web_port +
      " > '" + web_log + "' 2>&1 &";
    const int started = system(start_command.c_str());
    expect(started == 0, "the web server should start");

    // Give the server a moment to bind the port.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    hyprshare::http::Client client;
    const std::string base = "http://127.0.0.1:" + web_port;

    const auto status = client.get(base + "/api/status");
    expect(status.has_value(), "the status route should answer: " +
      (status.has_value() ? "" : status.error()));
    if (status.has_value()) {
      expect(status->status == 200, "the status route should answer with 200");
      expect(status->body.find("\"version\"") != std::string::npos,
        "the status route should report the version: " + status->body);
    }

    const auto health = client.get(base + "/api/health");
    expect(health.has_value(), "the health route should answer");
    if (health.has_value()) {
      expect(health->body.find("\"checks\"") != std::string::npos,
        "the health route should report the checks: " + health->body);
    }

    const auto config = client.get(base + "/api/config");
    expect(config.has_value(), "the config route should answer");
    if (config.has_value()) {
      expect(config->body.find("\"log_level\"") != std::string::npos,
        "the config route should report the settings: " + config->body);
    }

    const auto backends = client.get(base + "/api/backends");
    expect(backends.has_value(), "the backends route should answer");
    if (backends.has_value()) {
      expect(backends->body.find("portal-screencast") != std::string::npos,
        "the backends route should list the backends: " + backends->body);
    }

    const auto peers = client.get(base + "/api/peers");
    expect(peers.has_value(), "the peers route should answer");
    if (peers.has_value()) {
      expect(peers->body.find("\"peers\"") != std::string::npos,
        "the peers route should report the peers: " + peers->body);
    }

    const auto inbox = client.get(base + "/api/inbox");
    expect(inbox.has_value(), "the inbox route should answer");
    if (inbox.has_value()) {
      expect(inbox->body.find("\"receive_dir\"") != std::string::npos,
        "the inbox route should report the receive directory: " + inbox->body);
    }

    // A file can be uploaded into the inbox of this machine.
    {
      const std::string upload_file = temp_root.string() + "/upload.txt";
      {
        std::ofstream out(upload_file);
        out << "uploaded content";
      }

      const std::string boundary = "----hyprshare-test";
      std::string form;
      form += "--" + boundary + "\r\n";
      form += "Content-Disposition: form-data; name=\"files\"; filename=\"upload.txt\"\r\n";
      form += "\r\n";
      form += "uploaded content\r\n";
      form += "--" + boundary + "--\r\n";

      hyprshare::http::RequestOptions upload_options;
      const auto uploaded = client.post(base + "/api/upload", form,
        "multipart/form-data; boundary=" + boundary, upload_options);
      expect(uploaded.has_value(), "the upload route should answer: " +
        (uploaded.has_value() ? "" : uploaded.error()));
      if (uploaded.has_value()) {
        expect(uploaded->status == 200, "the upload should be accepted");
        expect(uploaded->body.find("\"files\":1") != std::string::npos,
          "the upload should report the file count: " + uploaded->body);
      }

      // The receive directory is resolved against the home of the test.
      const auto stored = temp_root / "home" / "Downloads" / "hyprshare" / "upload.txt";
      expect(std::filesystem::exists(stored), "the uploaded file should be in the receive directory");
      if (std::filesystem::exists(stored)) {
        std::ifstream in(stored);
        std::string content;
        std::getline(in, content);
        expect(content == "uploaded content", "the uploaded content should match");
      }
    }

    // An unknown route is a 404, not a crash.
    const auto missing_route = client.get(base + "/api/does-not-exist");
    expect(missing_route.has_value(), "an unknown route should still answer");
    if (missing_route.has_value()) {
      expect(missing_route->status == 404, "an unknown route should answer with 404");
    }

    // The server must be reachable on the loopback address only, because the
    // API exposes the local configuration. Connecting to 0.0.0.0 would not
    // show that, because the kernel treats it as the loopback address, so a
    // real address of this machine is used.
    const auto lan_address = first_non_loopback_ipv4();
    if (!lan_address.empty()) {
      const auto external = client.get("http://" + lan_address + ":" + web_port + "/api/status");
      expect(!external.has_value(),
        "the web server should not be reachable on the network address by default");
    }

    system(("pkill -f 'hyprshare web serve --port " + web_port + "' > /dev/null 2>&1").c_str());
  }

  // `remote status` used to print a hardcoded "not running" even while a
  // server was alive. Exercise the real start/status/stop state transition.
  {
    const std::string remote_port = "28984";

    const auto before = run_hyprshare(binary, "remote status --json");
    expect(before.exit_code == 0, "`remote status --json` should succeed");
    expect(before.output.find("\"status\":\"not_running\"") != std::string::npos,
      "remote status should report not_running when no daemon exists: " + before.output);

    const auto started = run_hyprshare(binary, "remote serve --port " + remote_port + " --json");
    expect(started.output.find("\"status\":\"started\"") != std::string::npos,
      "`remote serve` should report a real detached process: " + started.output);

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    const auto during = run_hyprshare(binary, "remote status --json");
    expect(during.output.find("\"status\":\"running\"") != std::string::npos,
      "remote status should reflect the running daemon: " + during.output);
    expect(during.output.find("\"port\":" + remote_port) != std::string::npos,
      "remote status should report the actual listening port: " + during.output);

    const auto stopped = run_hyprshare(binary, "remote stop --json");
    expect(stopped.output.find("\"status\":\"stopped\"") != std::string::npos,
      "`remote stop` should report the daemon it stopped: " + stopped.output);

    const auto after = run_hyprshare(binary, "remote status --json");
    expect(after.output.find("\"status\":\"not_running\"") != std::string::npos,
      "remote status should report not_running after stop: " + after.output);
    expect(after.output.find("\"port\":") == std::string::npos,
      "remote status must not expose a stale port after stop: " + after.output);
  }

  // The mobile server follows the same detached lifecycle, so a phone can reach
  // a server that outlives the starting command.
  {
    const std::string mobile_port = "28986";

    const auto before = run_hyprshare(binary, "mobile status --json");
    expect(before.output.find("\"status\":\"not_running\"") != std::string::npos,
      "mobile status should report not_running when no server exists: " + before.output);

    const auto started = run_hyprshare(binary,
      "mobile serve --port " + mobile_port + " --json");
    expect(started.output.find("\"status\":\"started\"") != std::string::npos,
      "`mobile serve` should detach a real server: " + started.output);

    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    const auto during = run_hyprshare(binary, "mobile status --json");
    expect(during.output.find("\"status\":\"running\"") != std::string::npos,
      "mobile status should reflect the running server: " + during.output);
    expect(during.output.find("\"port\":" + mobile_port) != std::string::npos,
      "mobile status should report the listening port: " + during.output);

    const auto stopped = run_hyprshare(binary, "mobile stop --json");
    expect(stopped.output.find("\"status\":\"stopped\"") != std::string::npos,
      "`mobile stop` should report the server it stopped: " + stopped.output);

    const auto after = run_hyprshare(binary, "mobile status --json");
    expect(after.output.find("\"status\":\"not_running\"") != std::string::npos,
      "mobile status should report not_running after stop: " + after.output);
  }

  // `share file` stages a share session by default, so the transfer chain is
  // one command away; --dry-run only inspects the manifest.
  {
    const RunResult staged_default = run_hyprshare(binary, "share file " + share_file);
    expect(staged_default.exit_code == 0,
      "`share file` should succeed by default: " + staged_default.output);
    expect(staged_default.output.find("Session:") != std::string::npos,
      "`share file` should stage a session by default: " + staged_default.output);
    expect(staged_default.output.find("session serve") != std::string::npos,
      "`share file` should print the serve next step: " + staged_default.output);
    expect(staged_default.output.find("Transport will be added later") == std::string::npos,
      "`share file` should not claim the transport is missing: " + staged_default.output);

    const RunResult listed = run_hyprshare(binary, "session list --json");
    expect(listed.exit_code == 0, "`session list --json` should succeed");
    expect(listed.output.find("probe.txt") != std::string::npos,
      "the staged session should appear in `session list`: " + listed.output);

    const RunResult inspected = run_hyprshare(binary, "share file " + share_file + " --dry-run --json");
    expect(inspected.exit_code == 0,
      "`share file --dry-run` should succeed: " + inspected.output);
    expect(inspected.output.find("session_id") == std::string::npos,
      "`share file --dry-run` should not stage a session: " + inspected.output);
    expect(inspected.output.find("probe.txt") != std::string::npos,
      "`share file --dry-run` should print the manifest: " + inspected.output);
  }

  // The desktop app parses --json output, so a command that silently falls
  // back to prose breaks the UI even though its exit code is 0. These ten
  // commands used to do exactly that.
  {
    const std::string json_commands[] = {
      "cloud configure probe --type local --endpoint " + temp_root.string() + "/cloud --json",
      "profile create jsonprobe --json",
      "web status --json",
      "batch run-commands true --json",
      "sync add " + share_file + " probe-device --address 127.0.0.1:9 --json",
      "monitor start --json",
      "monitor status --json",
      "monitor stop --json",
      "profiling start jsonprobe --json",
      "profiling stop jsonprobe --json",
      "profiling clear --json",
      "mobile status --json",
      "mobile stop --json",
    };
    for (const auto& args : json_commands) {
      const RunResult result = run_hyprshare(binary, args);
      const auto first = result.output.empty() ? '\0' : result.output.front();
      expect(result.exit_code == 0 && (first == '{' || first == '['),
        "`" + args + "` should emit JSON: " + result.output);
    }
  }

  // `share clipboard` follows the same contract: the default stages a
  // session, --dry-run inspects the text only. The clipboard write/read here
  // also guards the wl-clipboard fd bug (stdin must stay a valid fd).
  {
    const RunResult written = run_hyprshare(binary, "clipboard write --text hyprshare-clip-probe");
    expect(written.exit_code == 0,
      "`clipboard write` should succeed: " + written.output);

    const RunResult read = run_hyprshare(binary, "clipboard read --json");
    expect(read.exit_code == 0,
      "`clipboard read` should succeed (wl-clipboard valid stdin fd): " + read.output);
    expect(read.output.find("hyprshare-clip-probe") != std::string::npos,
      "clipboard read should return the written text: " + read.output);

    const RunResult inspected = run_hyprshare(binary, "share clipboard --dry-run");
    expect(inspected.exit_code == 0,
      "`share clipboard --dry-run` should succeed: " + inspected.output);
    expect(inspected.output.find("Session:") == std::string::npos,
      "`share clipboard --dry-run` should not stage: " + inspected.output);

    const RunResult staged = run_hyprshare(binary, "share clipboard");
    expect(staged.exit_code == 0,
      "`share clipboard` should stage by default: " + staged.output);
    expect(staged.output.find("Session:") != std::string::npos,
      "`share clipboard` default should create a session: " + staged.output);
    expect(staged.output.find("session serve") != std::string::npos,
      "`share clipboard` default should print the serve next step: " + staged.output);
  }

  // The plugin loader is exercised end to end: a real shared object is loaded
  // through the CLI, and the registration must survive across processes.
  if (argc >= 3) {
    const std::string plugin_path = argv[2];

    const RunResult loaded = run_hyprshare(binary, "plugin load " + plugin_path);
    expect(loaded.exit_code == 0,
      "`hyprshare plugin load` should succeed: " + loaded.output);
    expect(loaded.output.find("hello") != std::string::npos,
      "`hyprshare plugin load` should report the plugin name: " + loaded.output);

    // A separate process must still see the registered plugin.
    const RunResult listed = run_hyprshare(binary, "plugin list");
    expect(listed.exit_code == 0, "`hyprshare plugin list` should succeed");
    expect(listed.output.find("hello") != std::string::npos,
      "`hyprshare plugin list` should show the registered plugin: " + listed.output);

    const RunResult commands = run_hyprshare(binary, "plugin commands");
    expect(commands.exit_code == 0, "`hyprshare plugin commands` should succeed");
    expect(commands.output.find("hello") != std::string::npos,
      "`hyprshare plugin commands` should list the plugin command: " + commands.output);

    // A plugin command is dispatched like a built-in command.
    const RunResult plugin_command = run_hyprshare(binary, "hello");
    expect(plugin_command.exit_code == 0,
      "a plugin command should run successfully: " + plugin_command.output);
    expect(plugin_command.output.find("Hello from hyprshare plugin") != std::string::npos,
      "a plugin command should produce its own output: " + plugin_command.output);

    // The plugin command is advertised in the top-level help.
    const RunResult help_with_plugin = run_hyprshare(binary, "--help");
    expect(help_with_plugin.output.find("hyprshare hello") != std::string::npos,
      "the top-level help should list plugin commands: " + help_with_plugin.output);

    const RunResult disabled = run_hyprshare(binary, "plugin disable hello");
    expect(disabled.exit_code == 0, "`hyprshare plugin disable` should succeed");

    const RunResult after_disable = run_hyprshare(binary, "plugin list");
    expect(after_disable.output.find("disabled") != std::string::npos,
      "a disabled plugin should be reported as disabled: " + after_disable.output);

    const RunResult enabled = run_hyprshare(binary, "plugin enable hello");
    expect(enabled.exit_code == 0, "`hyprshare plugin enable` should succeed");

    const RunResult unloaded = run_hyprshare(binary, "plugin unload hello");
    expect(unloaded.exit_code == 0, "`hyprshare plugin unload` should succeed");

    const RunResult after_unload = run_hyprshare(binary, "plugin list");
    expect(after_unload.output.find("hello") == std::string::npos,
      "an unregistered plugin should no longer be listed: " + after_unload.output);
  }

  std::filesystem::remove_all(temp_root);

  if (failures == 0) {
    std::cout << "All CLI dispatch tests passed.\n";
  }
  return failures == 0 ? 0 : 1;
}
