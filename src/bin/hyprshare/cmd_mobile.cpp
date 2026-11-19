#include "cmd_mobile.hpp"

#include <iostream>
#include <thread>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/mobile/mobile_transfer.hpp"
#include "hyprshare/remote/remote_control.hpp"

namespace hyprshare::cli {

std::expected<int, std::string> handle_mobile(CommandContext&, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string("usage: hyprshare mobile <serve|send>"));
  }

  if (args[1] == "serve") {
    const auto port_str = take_option(args, "--port");
    const uint16_t port = port_str ? static_cast<uint16_t>(std::stoul(*port_str)) : 28556;

    hyprshare::mobile::MobileTransferServer server(port);
    auto start_result = server.start();
    if (!start_result.has_value()) {
      std::cerr << "error: " << start_result.error() << "\n";
      return 1;
    }

    std::cout << "Mobile transfer server started on port " << port << std::endl;
    std::cout << "Press Ctrl+C to stop." << std::endl;

    while (server.is_running()) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
  }

  if (args[1] == "send") {
    if (args.size() < 4) {
      return std::unexpected(std::string("usage: hyprshare mobile send <device_id> <file_path>"));
    }

    const auto device_id = args[2];
    const auto file_path = args[3];

    if (!std::filesystem::exists(file_path)) {
      return std::unexpected("file not found: " + file_path);
    }

    hyprshare::mobile::MobileTransferServer server;
    auto send_result = server.send_file(device_id, file_path);
    if (!send_result.has_value()) {
      std::cerr << "error: " << send_result.error() << "\n";
      return 1;
    }

    std::cout << "File sent to " << device_id << "\n";
    return 0;
  }

  return std::unexpected("unknown mobile subcommand: " + args[1]);
}

std::expected<int, std::string> handle_remote(CommandContext&, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string("usage: hyprshare remote <serve|status>"));
  }

  if (args[1] == "serve") {
    const auto port_str = take_option(args, "--port");
    const uint16_t port = port_str ? static_cast<uint16_t>(std::stoul(*port_str)) : 28557;

    hyprshare::remote::RemoteControlServer server(port);
    auto start_result = server.start();
    if (!start_result.has_value()) {
      std::cerr << "error: " << start_result.error() << "\n";
      return 1;
    }

    server.set_input_callback([](const hyprshare::remote::InputEvent& event) {
      std::cout << "Remote input: type=" << static_cast<int>(event.type)
                << " x=" << event.x << " y=" << event.y << "\n";
    });

    std::cout << "Remote control server started on port " << port << std::endl;
    std::cout << "Press Ctrl+C to stop." << std::endl;

    while (server.is_running()) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
  }

  if (args[1] == "status") {
    std::cout << "Remote control: not running\n";
    return 0;
  }

  return std::unexpected("unknown remote subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
