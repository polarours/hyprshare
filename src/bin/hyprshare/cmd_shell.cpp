#include "cmd_shell.hpp"

#include <iostream>

#include "command_utils.hpp"
#include "hyprshare/shell/interactive_shell.hpp"

namespace hyprshare::cli {

std::expected<int, std::string> handle_shell(CommandContext& ctx, std::vector<std::string>&) {
  hyprshare::shell::InteractiveShell shell;

  // Register hyprshare commands
  shell.register_command({"status", "Show system status", [&ctx](const std::vector<std::string>&) {
    if (ctx.json) {
      std::cout << "{\"status\":\"ok\"}\n";
    } else {
      std::cout << "hyprshare status: OK\n";
    }
    return 0;
  }});

  shell.register_command({"doctor", "Run health diagnostics", [&ctx](const std::vector<std::string>&) {
    std::cout << "Running diagnostics...\n";
    return 0;
  }});

  shell.register_command({"inbox", "Inbox file sharing", [](const std::vector<std::string>& cmd_args) {
    if (cmd_args.empty()) {
      std::cout << "Usage: inbox <serve|put|status>\n";
      return 0;
    }
    std::cout << "inbox: " << cmd_args[0] << "\n";
    return 0;
  }});

  shell.register_command({"session", "Session management", [](const std::vector<std::string>& cmd_args) {
    if (cmd_args.empty()) {
      std::cout << "Usage: session <list|serve|fetch>\n";
      return 0;
    }
    std::cout << "session: " << cmd_args[0] << "\n";
    return 0;
  }});

  shell.register_command({"peer", "Peer management", [](const std::vector<std::string>& cmd_args) {
    if (cmd_args.empty()) {
      std::cout << "Usage: peer <self|list|add>\n";
      return 0;
    }
    std::cout << "peer: " << cmd_args[0] << "\n";
    return 0;
  }});

  shell.register_command({"sync", "File synchronization", [](const std::vector<std::string>& cmd_args) {
    if (cmd_args.empty()) {
      std::cout << "Usage: sync <start|stop|status>\n";
      return 0;
    }
    std::cout << "sync: " << cmd_args[0] << "\n";
    return 0;
  }});

  shell.register_command({"clipboard-sync", "Clipboard synchronization", [](const std::vector<std::string>& cmd_args) {
    if (cmd_args.empty()) {
      std::cout << "Usage: clipboard-sync <start|stop|status>\n";
      return 0;
    }
    std::cout << "clipboard-sync: " << cmd_args[0] << "\n";
    return 0;
  }});

  shell.register_command({"record", "Screen recording", [](const std::vector<std::string>& cmd_args) {
    if (cmd_args.empty()) {
      std::cout << "Usage: record <start|stop|status>\n";
      return 0;
    }
    std::cout << "record: " << cmd_args[0] << "\n";
    return 0;
  }});

  shell.register_command({"config", "Configuration management", [](const std::vector<std::string>& cmd_args) {
    if (cmd_args.empty()) {
      std::cout << "Usage: config <show|set|get>\n";
      return 0;
    }
    std::cout << "config: " << cmd_args[0] << "\n";
    return 0;
  }});

  shell.register_command({"tls", "TLS certificate management", [](const std::vector<std::string>& args) {
    if (args.empty()) {
      std::cout << "Usage: tls <generate|verify|info>\n";
      return 0;
    }
    std::cout << "tls: " << args[0] << "\n";
    return 0;
  }});

  shell.run();
  return 0;
}

}  // namespace hyprshare::cli
