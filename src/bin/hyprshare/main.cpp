#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "hyprshare/core/adapter_registry.hpp"
#include "hyprshare/core/backend_registry.hpp"
#include "hyprshare/core/config.hpp"
#include "hyprshare/core/launcher.hpp"
#include "hyprshare/core/planner.hpp"
#include "hyprshare/diagnostics/doctor.hpp"
#include "hyprshare/platform/paths.hpp"
#include "hyprshare/platform/process.hpp"
#include "hyprshare/plugin/plugin_manager.hpp"
#include "hyprshare/plugin/plugin_registry.hpp"

#include "cmd_app.hpp"
#include "cmd_audit.hpp"
#include "cmd_backend.hpp"
#include "cmd_backup.hpp"
#include "cmd_batch.hpp"
#include "cmd_clipboard.hpp"
#include "cmd_clipboard_sync.hpp"
#include "cmd_cloud.hpp"
#include "cmd_compat.hpp"
#include "cmd_config.hpp"
#include "cmd_daemon.hpp"
#include "cmd_discovery.hpp"
#include "cmd_doctor.hpp"
#include "cmd_inbox.hpp"
#include "cmd_mobile.hpp"
#include "cmd_monitor.hpp"
#include "cmd_netdiag.hpp"
#include "cmd_peer.hpp"
#include "cmd_plugin.hpp"
#include "cmd_profile.hpp"
#include "cmd_profiling.hpp"
#include "cmd_record.hpp"
#include "cmd_search.hpp"
#include "cmd_session.hpp"
#include "cmd_share.hpp"
#include "cmd_shell.hpp"
#include "cmd_status.hpp"
#include "cmd_sync.hpp"
#include "cmd_tls.hpp"
#include "cmd_update.hpp"
#include "cmd_web.hpp"
#include "command_registry.hpp"
#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"

namespace {

template <typename T>
T load_or_exit(std::expected<T, std::string> result) {
  if (!result.has_value()) {
    std::cerr << "error: " << result.error() << "\n";
    std::exit(1);
  }
  return std::move(*result);
}

// Plugins contribute extra top-level commands. They are only loaded when they
// are actually needed, so a normal command does not pay for the loading.
//
// The plugin manager must stay alive while a plugin command runs: unloading
// the shared object invalidates the command handlers it owns.
template <typename Fn>
void with_plugin_commands(const hyprshare::platform::Paths& paths, Fn&& fn) {
  hyprshare::plugin::PluginManager manager;
  hyprshare::plugin::PluginRegistry registry(paths.config_dir / "plugins.list");
  static_cast<void>(hyprshare::plugin::load_registered(manager, registry));
  fn(manager.all_commands());
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  const bool json = hyprshare::cli::take_flag(args, "--json");
  const bool dry_run = hyprshare::cli::take_flag(args, "--dry-run");
  const bool stage = hyprshare::cli::take_flag(args, "--stage");
  const bool insecure = hyprshare::cli::take_flag(args, "--insecure");

  hyprshare::tls::TlsTransportConfig tls_config;
  const auto tls_cert = hyprshare::cli::take_option(args, "--tls-cert");
  const auto tls_key = hyprshare::cli::take_option(args, "--tls-key");
  const auto tls_ca = hyprshare::cli::take_option(args, "--tls-ca");
  const bool tls_enabled = hyprshare::cli::take_flag(args, "--tls");

  if (tls_enabled || tls_cert || tls_key || tls_ca) {
    tls_config.enabled = true;
    tls_config.verify_peer = !insecure;
    if (tls_cert) {
      tls_config.cert_path = *tls_cert;
    }
    if (tls_key) {
      tls_config.key_path = *tls_key;
    }
    if (tls_ca) {
      tls_config.ca_path = *tls_ca;
    }
  }

  hyprshare::cli::CommandRegistry registry;
  registry.register_command("status", hyprshare::cli::handle_status,
    "hyprshare status [--json]");
  registry.register_command("doctor", hyprshare::cli::handle_doctor,
    "hyprshare doctor [--app <app>] [--fail-on warning|error] [--json]");
  registry.register_command("check", hyprshare::cli::handle_check,
    "hyprshare check [--json]");
  registry.register_command("support", hyprshare::cli::handle_support,
    "hyprshare support bundle [--to <dir>] [--app <app>] [--json]");
  registry.register_command("daemon", hyprshare::cli::handle_daemon,
    "hyprshare daemon <start|status|stop|inbox list|inbox start|inbox stop>\n"
    "hyprshare daemon inbox start [--host <host>] [--port <port>] [--to <dir>] [--token <token>]\n"
    "hyprshare daemon inbox stop <listener-id>");
  registry.register_command("discovery", hyprshare::cli::handle_discovery,
    "hyprshare discovery status [--json]\n"
    "hyprshare discovery browse [--json]");
  registry.register_command("backend", hyprshare::cli::handle_backend,
    "hyprshare backend list [--json]");
  registry.register_command("compat", hyprshare::cli::handle_compat,
    "hyprshare compat show <app> [--json]");
  registry.register_command("config", hyprshare::cli::handle_config,
    "hyprshare config show [--json]\n"
    "hyprshare config set <key> <value> [--json]\n"
    "hyprshare config get <key> [--json]");
  registry.register_command("clipboard", hyprshare::cli::handle_clipboard,
    "hyprshare clipboard read [--json]\n"
    "hyprshare clipboard receive <session-id> [--json]\n"
    "hyprshare clipboard write [--text <text> | --from-file <path>] [--json]");
  registry.register_command("inbox", hyprshare::cli::handle_inbox, R"(hyprshare inbox status [--json]
hyprshare inbox history [--json]
hyprshare inbox history clear [--json]
hyprshare inbox open [--dry-run] [--json]
hyprshare inbox announce [--port <port>] [--name <name>] [--token-required] [--dry-run] [--json]
hyprshare inbox receive [--port <port>] [--to <dir>] [--count <n|0>] [--timeout <seconds>] [--max-bytes <bytes>] [--token <token> | --generate-token] [--name <name>] [--no-announce] [--extract] [--json]
hyprshare inbox serve [--host <host>] [--port <port>] [--to <dir>] [--count <n|0>] [--timeout <seconds>] [--max-bytes <bytes>] [--token <token> | --generate-token] [--announce] [--announce-name <name>] [--extract] [--json]
hyprshare inbox put <host:port> <path...> [--name <name>] [--token <token>] [--json]
hyprshare inbox put --to-discovered <name> <path...> [--name <name>] [--token <token>] [--json]
hyprshare inbox extract <archive> [--to <dir>] [--dry-run] [--json])");
  registry.register_command("peer", hyprshare::cli::handle_peer, R"(hyprshare peer self [--json]
hyprshare peer list [--json]
hyprshare peer inspect <path> [--json]
hyprshare peer pair <path> [--json]
hyprshare peer invite create [--to <path>] [--json]
hyprshare peer invite inspect <path> [--json]
hyprshare peer invite respond <path> [--to <path>] [--json]
hyprshare peer invite complete <path> [--json]
hyprshare peer export [--to <path>] [--json]
hyprshare peer import <path> [--json]
hyprshare peer add <device-id> [--label <label>] [--json]
hyprshare peer remove <device-id> [--json])");
  registry.register_command("session", hyprshare::cli::handle_session, R"(hyprshare session list [--json]
hyprshare session show <id> [--json]
hyprshare session delete <id> [--json]
hyprshare session prune [--older-than <seconds> | --all] [--json]
hyprshare session verify <id> [--json]
hyprshare session probe <host:port> [--token <token>] [--with-self] [--json]
hyprshare session inspect <host:port> [--token <token>] [--with-self] [--json]
hyprshare session serve <id> [--host <host>] [--port <port>] [--timeout <seconds>] [--token <token> | --generate-token] [--expires-in <seconds>] [--allow-peer <device-id>] [--json]
hyprshare session fetch <host:port> [--token <token>] [--with-self] [--to <path>] [--json]
hyprshare session export <id> [--to <bundle>] [--json]
hyprshare session import <bundle> [--json]
hyprshare session receive <id> [--to <path>] [--json])");
  registry.register_command("share", hyprshare::cli::handle_share,
    "hyprshare share file <path> [--dry-run] [--json]\n"
    "hyprshare share folder <path> [--dry-run] [--json]\n"
    "hyprshare share clipboard [--dry-run] [--json]\n"
    "hyprshare share screen [--app <app>] [--dry-run] [--json]");
  registry.register_command("app", hyprshare::cli::handle_app,
    "hyprshare app launch <app> [--dry-run] [--json]");
  registry.register_command("sync", hyprshare::cli::handle_sync,
    "hyprshare sync <start|stop|status|add|remove|list|now>\n"
    "hyprshare sync add <local-path> <remote-device-id> --address <host:port> [--token <token>]\n"
    "hyprshare sync remove <rule-id>\n"
    "hyprshare sync now <rule-id>");
  registry.register_command("clipboard-sync", hyprshare::cli::handle_clipboard_sync,
    "hyprshare clipboard-sync <start|stop|status|push|pull|history|send|receive>\n"
    "hyprshare clipboard-sync push <content>\n"
    "hyprshare clipboard-sync send <host:port> [--token <token>] [--device <id>]\n"
    "hyprshare clipboard-sync receive <host:port> [--token <token>]");
  registry.register_command("record", hyprshare::cli::handle_record,
    "hyprshare record <start|stop|pause|resume|status>\n"
    "hyprshare record start [--output <path>] [--format mp4|webm|mkv] [--width <px>] [--height <px>] [--fps <n>]");
  registry.register_command("mobile", hyprshare::cli::handle_mobile,
    "hyprshare mobile <serve|status|stop|send>\n"
    "hyprshare mobile serve [--port <port>] [--foreground]\n"
    "hyprshare mobile send <device-id> <file-path>");
  registry.register_command("remote", hyprshare::cli::handle_remote,
    "hyprshare remote <serve|status|stop>\n"
    "hyprshare remote serve [--port <port>]");
  registry.register_command("shell", hyprshare::cli::handle_shell,
    "hyprshare shell");
  registry.register_command("netdiag", hyprshare::cli::handle_netdiag,
    "hyprshare netdiag <interfaces|test|latency|resolve>\n"
    "hyprshare netdiag latency <host> [port]\n"
    "hyprshare netdiag resolve <hostname>");
  registry.register_command("tls", hyprshare::cli::handle_tls_cmd,
    "hyprshare tls <generate|verify|info|status>\n"
    "hyprshare tls generate [common-name]\n"
    "hyprshare tls verify <cert-file>\n"
    "hyprshare tls info <cert-file>");
  registry.register_command("batch", hyprshare::cli::handle_batch,
    "hyprshare batch <run-file|run-commands>\n"
    "hyprshare batch run-file <file>\n"
    "hyprshare batch run-commands <command>...");
  registry.register_command("plugin", hyprshare::cli::handle_plugin,
    "hyprshare plugin <list|load|unload|enable|disable|commands>\n"
    "hyprshare plugin load <path>\n"
    "hyprshare plugin unload <name>\n"
    "hyprshare plugin enable <name>\n"
    "hyprshare plugin disable <name>\n"
    "hyprshare plugin commands");
  registry.register_command("monitor", hyprshare::cli::handle_monitor,
    "hyprshare monitor <start|stop|status|snapshot>");
  registry.register_command("audit", hyprshare::cli::handle_audit,
    "hyprshare audit <run|check|list>\n"
    "hyprshare audit check <check-id>");
  registry.register_command("backup", hyprshare::cli::handle_backup,
    "hyprshare backup <create|restore|delete|list|inspect>\n"
    "hyprshare backup create [description]\n"
    "hyprshare backup restore <backup-id>\n"
    "hyprshare backup delete <backup-id>\n"
    "hyprshare backup inspect <backup-id>");
  registry.register_command("cloud", hyprshare::cli::handle_cloud,
    "hyprshare cloud <configure|upload|download|sync|list|status>\n"
    "hyprshare cloud configure <provider> [--type local|http] [--endpoint <url>] [--bucket <name>] [--token <token>] [--secret <secret>]\n"
    "hyprshare cloud upload <local-path> <remote-path>\n"
    "hyprshare cloud download <remote-path> <local-path>\n"
    "hyprshare cloud sync <local-dir> <remote-dir>\n"
    "hyprshare cloud list <remote-dir>");
  registry.register_command("search", hyprshare::cli::handle_search,
    "hyprshare search files --path <dir> [--pattern <glob>] [--no-recursive] [--include-hidden] [--sort-by name|size|date|type] [--order asc|desc]");
  registry.register_command("update", hyprshare::cli::handle_update,
    "hyprshare update <check|download|apply|status>\n"
    "hyprshare update check [--manifest <file> | --url <url>]\n"
    "hyprshare update download [--manifest <file> | --url <url>]\n"
    "hyprshare update apply [--yes]");
  registry.register_command("profile", hyprshare::cli::handle_profile,
    "hyprshare profile <list|get|create|delete|switch|export|import>\n"
    "hyprshare profile get <profile-id>\n"
    "hyprshare profile create <name> [--email <email>]\n"
    "hyprshare profile delete <profile-id>\n"
    "hyprshare profile switch <profile-id>\n"
    "hyprshare profile export <profile-id> <output-path>\n"
    "hyprshare profile import <path>");
  registry.register_command("web", hyprshare::cli::handle_web,
    "hyprshare web <serve|status>\n"
    "hyprshare web serve [--port <port>] [--host <address>]");
  registry.register_command("profiling", hyprshare::cli::handle_profiling,
    "hyprshare profiling <start|stop|report|clear>\n"
    "hyprshare profiling start <name>\n"
    "hyprshare profiling stop <name>");

  const hyprshare::platform::Paths paths = hyprshare::platform::resolve_paths();

  if (!args.empty() && (args[0] == "--version" || args[0] == "-V")) {
    std::cout << HYPRSHARE_VERSION << "\n";
    return 0;
  }

  if (args.empty() || args[0] == "help" || args[0] == "--help") {
    registry.print_usage();
    with_plugin_commands(paths, [](const std::vector<hyprshare::plugin::PluginCommand>& commands) {
      if (commands.empty()) {
        return;
      }
      std::cout << "\nPlugin Commands:\n";
      for (const auto& command : commands) {
        std::cout << "  hyprshare " << command.name << " - " << command.description << "\n";
      }
    });
    return 0;
  }

  if (args.size() >= 2) {
    const bool wants_help = std::any_of(args.begin() + 1, args.end(), [](const std::string& arg) {
      return arg == "--help" || arg == "-h";
    });
    if (wants_help && registry.print_command_usage(args[0])) {
      return 0;
    }
  }

  const hyprshare::core::Config config = load_or_exit(hyprshare::core::load_config(paths));
  const hyprshare::core::BackendRegistry backends = load_or_exit(hyprshare::core::BackendRegistry::load(paths.data_dir / "backends"));
  const hyprshare::core::AdapterRegistry adapters = load_or_exit(hyprshare::core::AdapterRegistry::load(paths.data_dir / "compat" / "apps"));
  const hyprshare::platform::ProcessRunner runner;
  const hyprshare::diagnostics::Doctor doctor(runner);
  const hyprshare::core::SharePlanner planner;
  const hyprshare::core::AppLauncher launcher(runner);

  hyprshare::cli::CommandContext ctx{
    .paths = paths,
    .config = config,
    .backends = backends,
    .adapters = adapters,
    .runner = runner,
    .doctor = doctor,
    .planner = planner,
    .launcher = launcher,
    .json = json,
    .dry_run = dry_run,
    .stage = stage,
    .insecure = insecure,
    .tls_config = tls_config,
  };

  const auto result = registry.dispatch(args[0], ctx, args);
  if (result.has_value()) {
    return *result;
  }

  // The command is not built in: it may come from a plugin.
  {
    int plugin_exit_code = 1;
    bool plugin_command_found = false;
    with_plugin_commands(paths, [&](const std::vector<hyprshare::plugin::PluginCommand>& commands) {
      for (const auto& command : commands) {
        if (command.name != args[0]) {
          continue;
        }
        const std::vector<std::string> command_args(args.begin() + 1, args.end());
        const auto plugin_result = command.handler(command_args);
        if (!plugin_result.has_value()) {
          std::cerr << "error: " << plugin_result.error() << "\n";
          plugin_exit_code = 1;
        } else {
          plugin_exit_code = *plugin_result;
        }
        plugin_command_found = true;
        break;
      }
    });
    if (plugin_command_found) {
      return plugin_exit_code;
    }
  }

  std::cerr << "error: " << result.error() << "\n";
  return 1;
}
