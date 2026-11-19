#include "cmd_web.hpp"

#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "command_registry.hpp"
#include "command_utils.hpp"
#include "output_json.hpp"
#include "hyprshare/core/config.hpp"
#include "hyprshare/core/inbox_history.hpp"
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

std::expected<int, std::string> handle_web(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string("usage: hyprshare web <serve|status>"));
  }

  if (args[1] == "serve") {
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

    hyprshare::web::ApiServer server(port, host);
    register_routes(server, ctx);

    auto started = server.start();
    if (!started.has_value()) {
      std::cerr << "error: " << started.error() << "\n";
      return 1;
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
    std::cout << "Web API: not running\n";
    std::cout << "  Start it with: hyprshare web serve\n";
    return 0;
  }

  return std::unexpected("unknown web subcommand: " + args[1]);
}

}  // namespace hyprshare::cli
