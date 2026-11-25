#include "cmd_web.hpp"

#include <array>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

#include "command_registry.hpp"
#include "command_utils.hpp"
#include "output_json.hpp"
#include "hyprshare/core/config.hpp"
#include "hyprshare/core/inbox_history.hpp"
#include "hyprshare/http/http_client.hpp"
#include "hyprshare/platform/net.hpp"
#include "hyprshare/web/multipart.hpp"
#include "hyprshare/core/trust_store.hpp"
#include "hyprshare/model/types.hpp"
#include "hyprshare/util/string.hpp"
#include "hyprshare/web/api_server.hpp"

namespace hyprshare::cli {

namespace {

std::string json_string(const std::string& value) {
  return "\"" + hyprshare::util::escape_json(value) + "\"";
}

// The value of a request header, looked up without regard to its case.
std::string request_header(const hyprshare::web::ApiRequest& request, const std::string& name) {
  for (const auto& header : request.headers) {
    if (hyprshare::util::to_lower(hyprshare::util::trim(header.first)) ==
        hyprshare::util::to_lower(name)) {
      return hyprshare::util::trim(header.second);
    }
  }
  return "";
}

// A file name that can be written to disk: no directories, no surprises.
std::string safe_file_name(const std::string& raw) {
  const auto base = std::filesystem::path(raw).filename().string();
  std::string safe;
  for (const unsigned char character : base) {
    if (std::isalnum(character) != 0 || character == '.' || character == '-' || character == '_') {
      safe.push_back(static_cast<char>(character));
    } else {
      safe.push_back('_');
    }
  }
  if (safe.empty() || safe == "." || safe == "..") {
    return "";
  }
  return safe;
}

std::string status_name(const hyprshare::model::CheckStatus status) {
  switch (status) {
    case hyprshare::model::CheckStatus::Pass: return "pass";
    case hyprshare::model::CheckStatus::Fail: return "fail";
    case hyprshare::model::CheckStatus::Warn: return "warn";
    case hyprshare::model::CheckStatus::Skip: return "skip";
  }
  return "skip";
}

// The routes of the API. They read the same data the command line shows, so
// the web interface and the CLI never disagree.
void register_routes(hyprshare::web::ApiServer& server, CommandContext& ctx) {
  server.register_handler("GET", "/api/status", [&ctx](const hyprshare::web::ApiRequest&) {
    hyprshare::web::ApiResponse response;
    response.body = "{";
    response.body += "\"version\":" + json_string(HYPRSHARE_VERSION);
    response.body += ",\"config_file\":" + json_string(ctx.paths.config_file.string());
    response.body += ",\"state_dir\":" + json_string(ctx.paths.state_dir.string());
    response.body += ",\"data_dir\":" + json_string(ctx.paths.data_dir.string());
    response.body += "}";
    return response;
  });

  server.register_handler("GET", "/api/health", [&ctx](const hyprshare::web::ApiRequest&) {
    const auto& report = ctx.doctor.run(ctx.paths, ctx.adapters, std::nullopt);

    hyprshare::web::ApiResponse response;
    std::size_t passed = 0;
    std::size_t failed = 0;
    std::size_t warned = 0;
    std::size_t skipped = 0;
    for (const auto& check : report.checks) {
      switch (check.status) {
        case hyprshare::model::CheckStatus::Pass: ++passed; break;
        case hyprshare::model::CheckStatus::Fail: ++failed; break;
        case hyprshare::model::CheckStatus::Warn: ++warned; break;
        case hyprshare::model::CheckStatus::Skip: ++skipped; break;
      }
    }

    response.body = "{\"checks\":" + std::to_string(report.checks.size());
    response.body += ",\"pass\":" + std::to_string(passed);
    response.body += ",\"fail\":" + std::to_string(failed);
    response.body += ",\"warn\":" + std::to_string(warned);
    response.body += ",\"skip\":" + std::to_string(skipped);
    response.body += ",\"results\":[";
    for (std::size_t index = 0; index < report.checks.size(); ++index) {
      if (index > 0) {
        response.body += ",";
      }
      const auto& check = report.checks[index];
      response.body += "{\"id\":" + json_string(check.id);
      response.body += ",\"status\":" + json_string(status_name(check.status));
      response.body += ",\"severity\":" + json_string(hyprshare::model::to_string(check.severity));
      response.body += ",\"summary\":" + json_string(check.summary);
      response.body += "}";
    }
    response.body += "]}";
    return response;
  });

  server.register_handler("GET", "/api/config", [&ctx](const hyprshare::web::ApiRequest&) {
    const auto& config = ctx.config;
    hyprshare::web::ApiResponse response;
    response.body = "{";
    response.body += "\"default_scope\":" + json_string(config.general.default_scope);
    response.body += ",\"discoverability\":" + json_string(config.general.discoverability);
    response.body += ",\"log_level\":" + json_string(config.general.log_level);
    response.body += ",\"receive_dir\":" + json_string(config.files.receive_dir);
    response.body += ",\"allow_fallback_to_obs\":" +
      std::string(config.screen.allow_fallback_to_obs ? "true" : "false");
    response.body += ",\"require_explicit_clipboard_approval\":" +
      std::string(config.security.require_explicit_clipboard_approval ? "true" : "false");
    response.body += ",\"default_session_ttl_minutes\":" +
      std::to_string(config.security.default_session_ttl_minutes);
    response.body += "}";
    return response;
  });

  server.register_handler("GET", "/api/backends", [&ctx](const hyprshare::web::ApiRequest&) {
    hyprshare::web::ApiResponse response;
    response.body = "{\"backends\":[";
    const auto& backends = ctx.backends.all();
    for (std::size_t index = 0; index < backends.size(); ++index) {
      if (index > 0) {
        response.body += ",";
      }
      const auto& backend = backends[index];
      response.body += "{\"id\":" + json_string(backend.id);
      response.body += ",\"summary\":" + json_string(backend.summary);
      response.body += ",\"launch_supported\":" +
        std::string(backend.launch_supported ? "true" : "false");
      response.body += ",\"optional\":" + std::string(backend.optional ? "true" : "false");
      response.body += "}";
    }
    response.body += "]}";
    return response;
  });

  server.register_handler("GET", "/api/peers", [&ctx](const hyprshare::web::ApiRequest&) {
    const auto peers = hyprshare::core::list_trusted_peers(ctx.paths);
    hyprshare::web::ApiResponse response;
    if (!peers.has_value()) {
      response.status = 500;
      response.body = "{\"error\":" + json_string(peers.error()) + "}";
      return response;
    }

    response.body = "{\"peers\":[";
    for (std::size_t index = 0; index < peers->size(); ++index) {
      if (index > 0) {
        response.body += ",";
      }
      const auto& peer = (*peers)[index];
      response.body += "{\"device_id\":" + json_string(peer.device_id);
      response.body += ",\"device_label\":" + json_string(peer.device_label);
      response.body += "}";
    }
    response.body += "]}";
    return response;
  });

  server.register_handler("POST", "/api/upload", [&ctx](const hyprshare::web::ApiRequest& request) {
    hyprshare::web::ApiResponse response;

    const auto content_type = request_header(request, "Content-Type");
    const auto boundary = hyprshare::web::multipart_boundary(content_type);
    if (boundary.empty()) {
      response.status = 400;
      response.body = "{\"error\":\"the request is not a multipart form\"}";
      return response;
    }

    const auto parts = hyprshare::web::parse_multipart(request.body, boundary);
    if (parts.empty()) {
      response.status = 400;
      response.body = "{\"error\":\"the request carries no file\"}";
      return response;
    }

    const auto receive_dir = hyprshare::platform::expand_home_path(ctx.config.files.receive_dir);
    std::error_code error;
    std::filesystem::create_directories(receive_dir, error);
    if (error) {
      response.status = 500;
      response.body = "{\"error\":" + json_string("failed to create the receive directory: " + error.message()) + "}";
      return response;
    }

    std::size_t stored = 0;
    for (const auto& part : parts) {
      const auto name = safe_file_name(part.filename);
      if (name.empty()) {
        continue;
      }

      const auto target = receive_dir / name;
      std::ofstream output(target, std::ios::binary | std::ios::trunc);
      if (!output.is_open()) {
        continue;
      }
      output << part.content;
      if (output.fail()) {
        continue;
      }
      ++stored;

      hyprshare::model::InboxReceivedFile received;
      received.file_name = name;
      received.destination_path = target.string();
      received.bytes_received = part.content.size();
      static_cast<void>(hyprshare::core::append_inbox_history(received, ctx.paths));
    }

    if (stored == 0) {
      response.status = 500;
      response.body = "{\"error\":\"none of the files could be stored\"}";
      return response;
    }

    response.body = "{\"files\":" + std::to_string(stored) + "}";
    return response;
  });

  server.register_handler("GET", "/api/inbox", [&ctx](const hyprshare::web::ApiRequest&) {
    const auto history = hyprshare::core::list_inbox_history(ctx.paths);
    hyprshare::web::ApiResponse response;
    response.body = "{\"receive_dir\":" + json_string(ctx.config.files.receive_dir);
    response.body += ",\"history_entries\":" +
      std::to_string(history.has_value() ? history->size() : 0);
    response.body += "}";
    return response;
  });
}

}  // namespace

namespace {

std::filesystem::path web_state_dir(const CommandContext& ctx) {
  return ctx.paths.state_dir / "web";
}

std::filesystem::path web_pid_file(const CommandContext& ctx) {
  return web_state_dir(ctx) / "server.pid";
}

std::filesystem::path web_port_file(const CommandContext& ctx) {
  return web_state_dir(ctx) / "server.port";
}

int read_int_file(const std::filesystem::path& path) {
  std::ifstream file(path);
  int value = -1;
  file >> value;
  return value;
}

std::string self_executable_path() {
  std::array<char, 4096> buffer{};
  const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (length <= 0) {
    return "hyprshare";
  }
  return std::string(buffer.data(), static_cast<std::size_t>(length));
}

bool web_process_alive(const int pid) {
  return pid > 0 && kill(pid, 0) == 0;
}

// The server runs in the foreground, so a Ctrl+C must not leave a stale pid
// file behind. The handler only unlinks the file and exits.
std::filesystem::path& cleanup_pid_path() {
  static std::filesystem::path path;
  return path;
}

void remove_pid_on_signal(int) {
  std::error_code error;
  if (!cleanup_pid_path().empty()) {
    std::filesystem::remove(cleanup_pid_path(), error);
  }
  std::_Exit(0);
}

}  // namespace

std::expected<int, std::string> handle_web(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string("usage: hyprshare web <serve|status|stop>"));
  }

  // Internal entry point used by `serve --detach`: the real foreground server.
  const bool is_daemon = args[1] == "_daemon";

  if (args[1] == "serve" || is_daemon) {
    const auto port_option = take_option(args, "--port");
    const auto host_option = take_option(args, "--host");
    // The API exposes the local configuration and the staged sessions, so it
    // stays on the loopback address unless the network is asked for.
    const std::string host = host_option.value_or("127.0.0.1");

    std::uint16_t port = 8080;
    if (port_option.has_value()) {
      try {
        port = static_cast<std::uint16_t>(std::stoul(*port_option));
      } catch (const std::exception&) {
        return std::unexpected("the port is not a number: " + *port_option);
      }
    }

    // `serve --detach` hands the real work to a child process so the caller (and
    // therefore the desktop app) gets its terminal back.
    if (!is_daemon && take_flag(args, "--detach")) {
      const int existing = read_int_file(web_port_file(ctx));
      const int existing_pid = read_int_file(web_pid_file(ctx));
      if (existing > 0 && web_process_alive(existing_pid) &&
          hyprshare::platform::connect_transport_socket(
            "127.0.0.1", static_cast<std::uint16_t>(existing & 0xFFFF))
            .has_value()) {
        if (ctx.json) {
          std::cout << "{\"status\":\"already_running\",\"pid\":" << existing_pid
                    << ",\"port\":" << existing << "}\n";
        } else {
          std::cout << "Web API is already running on port " << existing << " (pid "
                    << existing_pid << ")\n";
        }
        return 0;
      }

      std::error_code error;
      std::filesystem::create_directories(web_state_dir(ctx), error);
      std::filesystem::remove(web_pid_file(ctx), error);

      std::string command = util::shell_quote(self_executable_path()) + " web _daemon --port " +
                            std::to_string(port) + " --host " + util::shell_quote(host);
      auto pid = ctx.runner.spawn_detached_shell(command);
      if (!pid.has_value()) {
        std::cerr << "error: " << pid.error() << "\n";
        return 1;
      }

      bool answering = false;
      for (int attempt = 0; attempt < 50; ++attempt) {
        if (hyprshare::platform::connect_transport_socket(
              "127.0.0.1", port)
              .has_value()) {
          answering = true;
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      if (!answering) {
        return std::unexpected("the web server did not start listening on port " +
                               std::to_string(port));
      }

      {
        std::ofstream pid_file(web_pid_file(ctx));
        pid_file << *pid << "\n";
        std::ofstream port_file(web_port_file(ctx));
        port_file << port << "\n";
      }
      if (ctx.json) {
        std::cout << "{\"status\":\"started\",\"pid\":" << *pid << ",\"port\":" << port
                  << ",\"url\":\"http://" << host << ":" << port << "\"}\n";
      } else {
        std::cout << "Web API started on http://" << host << ":" << port << " (pid " << *pid
                  << ")\n";
        std::cout << "Stop it with: hyprshare web stop\n";
      }
      return 0;
    }

    hyprshare::web::ApiServer server(port, host);
    register_routes(server, ctx);

    // Serve the frontend next to the API when built into the install tree.
    // HYPRSHARE_WEB_DIR is the explicit override; the other candidates cover
    // the AUR layout (/usr/share/hyprshare/web), a local build tree, and the
    // working directory.
    std::vector<std::filesystem::path> web_candidates;
    if (const char* from_env = std::getenv("HYPRSHARE_WEB_DIR")) {
      if (from_env[0] != '\0') web_candidates.emplace_back(from_env);
    }
    web_candidates.emplace_back("/usr/share/hyprshare/web");
    web_candidates.emplace_back("web/dist");
    web_candidates.emplace_back(ctx.paths.source_dir / "web" / "dist");
    for (const auto& candidate : web_candidates) {
      if (std::filesystem::exists(candidate / "index.html")) {
        server.set_static_dir(candidate);
        break;
      }
    }

    auto started = server.start();
    if (!started.has_value()) {
      std::cerr << "error: " << started.error() << "\n";
      return 1;
    }

    // Record where this server runs so `web status` and `web stop` can find it.
    {
      std::error_code error;
      std::filesystem::create_directories(web_state_dir(ctx), error);
      std::ofstream pid_file(web_pid_file(ctx));
      pid_file << getpid() << "\n";
      std::ofstream port_file(web_port_file(ctx));
      port_file << port << "\n";
      cleanup_pid_path() = web_pid_file(ctx);
      std::signal(SIGINT, remove_pid_on_signal);
      std::signal(SIGTERM, remove_pid_on_signal);
    }

    std::cout << "  GET http://" << host << ":" << port << "/api/status" << std::endl;
    std::cout << "  GET http://" << host << ":" << port << "/api/health" << std::endl;
    std::cout << "  GET http://" << host << ":" << port << "/api/config" << std::endl;
    std::cout << "  GET http://" << host << ":" << port << "/api/backends" << std::endl;
    std::cout << "  GET http://" << host << ":" << port << "/api/peers" << std::endl;
    std::cout << "  GET http://" << host << ":" << port << "/api/inbox" << std::endl;
    std::cout << "Press Ctrl+C to stop." << std::endl;

    while (server.is_running()) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
  }

  if (args[1] == "status") {
    // Probe the local port instead of asserting "not running": the server runs
    // as a separate process, so the only trustworthy signal is whether the
    // socket answers. Without --port, prefer the port recorded by `serve`.
    const auto port_str = take_option(args, "--port");
    std::uint16_t port = 8080;
    if (port_str.has_value()) {
      port = static_cast<std::uint16_t>(std::stoul(*port_str) & 0xFFFFu);
    } else if (const int recorded = read_int_file(web_port_file(ctx)); recorded > 0) {
      port = static_cast<std::uint16_t>(recorded & 0xFFFF);
    }
    const int pid = read_int_file(web_pid_file(ctx));
    const auto connection = hyprshare::platform::connect_transport_socket("127.0.0.1", port);
    const bool answered = connection.has_value();

    std::string version;
    if (answered) {
      hyprshare::http::Client client;
      const auto response = client.get("http://127.0.0.1:" + std::to_string(port) + "/api/status");
      if (response.has_value()) {
        const auto& body = response->body;
        const auto key = body.find("\"version\":\"");
        if (key != std::string::npos) {
          const auto start = key + 11;
          const auto end = body.find('"', start);
          if (end != std::string::npos) {
            version = body.substr(start, end - start);
          }
        }
      }
    }

    // "Running" must mean *our* server: a live pid we recorded, or a listener
    // that identifies itself through /api/status. 8080 is commonly taken, and a
    // bare TCP probe used to report a foreign process as our server.
    const bool own_process = pid > 0 && web_process_alive(pid);
    const bool running = own_process || !version.empty();

    if (ctx.json) {
      std::cout << "{\"running\":" << (running ? "true" : "false")
                << ",\"host\":\"127.0.0.1\",\"port\":" << port;
      if (running && pid > 0) {
        std::cout << ",\"pid\":" << pid;
      }
      if (!version.empty()) {
        std::cout << ",\"version\":\"" << version << "\"";
      }
      if (!running && answered) {
        // Something else holds the port; say so instead of implying we are down
        // because nothing is listening.
        std::cout << ",\"port_in_use\":true";
      }
      std::cout << "}\n";
    } else {
      if (running) {
        std::cout << "Web API: running on http://127.0.0.1:" << port << "\n";
        if (pid > 0) {
          std::cout << "  pid: " << pid << "\n";
        }
        std::cout << "  Open it in a browser to use the web UI\n";
      } else {
        std::cout << "Web API: not running on port " << port << "\n";
        if (answered) {
          std::cout << "  Note: another process is listening on that port\n";
        }
        std::cout << "  Start it with: hyprshare web serve --port " << port << "\n";
      }
    }
    return 0;
  }

  if (args[1] == "stop") {
    const int pid = read_int_file(web_pid_file(ctx));
    const int port = read_int_file(web_port_file(ctx));
    const bool alive = web_process_alive(pid);
    if (alive) {
      kill(pid, SIGTERM);
      // Wait for the socket to close so the caller can trust the result.
      for (int attempt = 0; attempt < 30; ++attempt) {
        if (!hyprshare::platform::connect_transport_socket(
               "127.0.0.1", static_cast<std::uint16_t>(port & 0xFFFF))
               .has_value()) {
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
    std::error_code error;
    // Keep the recorded port as a hint for the next `status`; only the pid is
    // meaningless once the process is gone.
    std::filesystem::remove(web_pid_file(ctx), error);

    if (ctx.json) {
      std::cout << "{\"status\":\"" << (alive ? "stopped" : "not_running")
                << "\",\"pid\":" << (alive ? pid : 0) << "}\n";
    } else {
      std::cout << "Web API server " << (alive ? "stopped" : "was not running") << "\n";
    }
    return 0;
  }

  return std::unexpected("unknown web subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
