#include "cmd_session.hpp"

#include <filesystem>
#include <iostream>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/core/file_share.hpp"
#include "hyprshare/core/trust_store.hpp"

namespace hyprshare::cli {

using hyprshare::model::ReceivedFileShare;
using hyprshare::model::StagedFileShare;

std::expected<int, std::string> handle_session(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string("unknown session subcommand"));
  }

  if (args[1] == "list") {
    const auto sessions = hyprshare::core::list_staged_file_shares(ctx.paths);
    if (!sessions.has_value()) {
      std::cerr << "error: " << sessions.error() << "\n";
      return 1;
    }
    if (ctx.json) {
      print_session_list_json(*sessions);
    } else {
      print_session_list_text(*sessions);
    }
    return 0;
  }

  if (args[1] == "show" && args.size() >= 3) {
    const auto staged = hyprshare::core::load_staged_file_share(args[2], ctx.paths);
    if (!staged.has_value()) {
      std::cerr << "error: " << staged.error() << "\n";
      return 1;
    }
    if (ctx.json) {
      print_staged_share_json(*staged);
    } else {
      print_staged_share_text(*staged);
    }
    return 0;
  }

  if (args[1] == "delete" && args.size() >= 3) {
    const auto deleted = hyprshare::core::delete_staged_file_share(args[2], ctx.paths);
    if (!deleted.has_value()) {
      std::cerr << "error: " << deleted.error() << "\n";
      return 1;
    }
    if (ctx.json) {
      print_deleted_session_json(args[2], *deleted);
    } else {
      print_deleted_session_text(args[2], *deleted);
    }
    return 0;
  }

  if (args[1] == "prune") {
    const bool prune_all = take_flag(args, "--all");
    const auto older_than = take_option(args, "--older-than");
    if (prune_all && older_than.has_value()) {
      std::cerr << "error: --all and --older-than are mutually exclusive\n";
      return 1;
    }
    if (!prune_all && !older_than.has_value()) {
      std::cerr << "error: session prune requires --all or --older-than <seconds>\n";
      return 1;
    }

    std::optional<std::uint32_t> older_than_seconds;
    if (older_than.has_value()) {
      const auto parsed = parse_ttl_value(*older_than);
      if (!parsed.has_value()) {
        std::cerr << "error: " << parsed.error() << "\n";
        return 1;
      }
      older_than_seconds = *parsed;
    }

    const auto pruned = hyprshare::core::prune_staged_file_shares(ctx.paths, older_than_seconds);
    if (!pruned.has_value()) {
      std::cerr << "error: " << pruned.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      print_pruned_sessions_json(*pruned);
    } else {
      print_pruned_sessions_text(*pruned);
    }
    return 0;
  }

  if (args[1] == "verify" && args.size() >= 3) {
    const auto staged = hyprshare::core::load_staged_file_share(args[2], ctx.paths);
    if (!staged.has_value()) {
      std::cerr << "error: " << staged.error() << "\n";
      return 1;
    }

    const auto verification = hyprshare::core::verify_staged_file_share(*staged, ctx.runner);
    if (!verification.has_value()) {
      std::cerr << "error: " << verification.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      print_verified_share_json(*staged);
    } else {
      print_verified_share_text(*staged);
    }
    return 0;
  }

  if (args[1] == "probe" && args.size() >= 3) {
    const auto token = take_option(args, "--token");
    const bool with_self = take_flag(args, "--with-self");
    const auto endpoint = parse_endpoint_value(args[2]);
    if (!endpoint.has_value()) {
      std::cerr << "error: " << endpoint.error() << "\n";
      return 1;
    }

    std::string peer_id;
    if (with_self) {
      const auto identity = hyprshare::core::load_or_create_device_identity(ctx.paths);
      if (!identity.has_value()) {
        std::cerr << "error: " << identity.error() << "\n";
        return 1;
      }
      peer_id = identity->device_id;
    }

    const auto probed = hyprshare::core::probe_remote_file_share(
      endpoint->first,
      endpoint->second,
      token.value_or(std::string()),
      peer_id,
      ctx.tls_config);
    if (!probed.has_value()) {
      if (ctx.json) {
        print_probe_json(endpoint->first, endpoint->second, false, probed.error());
      } else {
        print_probe_text(endpoint->first, endpoint->second, false, probed.error());
      }
      return 1;
    }

    if (ctx.json) {
      print_probe_json(endpoint->first, endpoint->second, true, "");
    } else {
      print_probe_text(endpoint->first, endpoint->second, true, "");
    }
    return 0;
  }

  if (args[1] == "inspect" && args.size() >= 3) {
    const auto token = take_option(args, "--token");
    const bool with_self = take_flag(args, "--with-self");
    const auto endpoint = parse_endpoint_value(args[2]);
    if (!endpoint.has_value()) {
      std::cerr << "error: " << endpoint.error() << "\n";
      return 1;
    }

    std::string peer_id;
    if (with_self) {
      const auto identity = hyprshare::core::load_or_create_device_identity(ctx.paths);
      if (!identity.has_value()) {
        std::cerr << "error: " << identity.error() << "\n";
        return 1;
      }
      peer_id = identity->device_id;
    }

    const auto offer = hyprshare::core::inspect_remote_file_share(
      endpoint->first,
      endpoint->second,
      token.value_or(std::string()),
      peer_id,
      ctx.tls_config);
    if (!offer.has_value()) {
      std::cerr << "error: " << offer.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      print_remote_offer_json(*offer);
    } else {
      print_remote_offer_text(*offer);
    }
    return 0;
  }

  if (args[1] == "serve" && args.size() >= 3) {
    const auto host_override = take_option(args, "--host");
    const auto port_override = take_option(args, "--port");
    const auto timeout_override = take_option(args, "--timeout");
    const auto token = take_option(args, "--token");
    const bool generate_token = take_flag(args, "--generate-token");
    const auto serve_token = resolve_receiver_token(token, generate_token, ctx.json);
    if (!serve_token.has_value()) {
      std::cerr << "error: " << serve_token.error() << "\n";
      return 1;
    }
    const auto ttl_override = take_option(args, "--expires-in");
    const auto allowed_peer = take_option(args, "--allow-peer");
    const auto staged = hyprshare::core::load_staged_file_share(args[2], ctx.paths);
    if (!staged.has_value()) {
      std::cerr << "error: " << staged.error() << "\n";
      return 1;
    }

    const std::string host = host_override.value_or("127.0.0.1");
    const auto port = port_override.has_value() ? parse_port_value(*port_override) : std::expected<std::uint16_t, std::string>(8848);
    if (!port.has_value()) {
      std::cerr << "error: " << port.error() << "\n";
      return 1;
    }

    if (ttl_override.has_value() && !serve_token->has_value()) {
      std::cerr << "error: --expires-in requires --token or --generate-token\n";
      return 1;
    }

    const auto token_ttl = ttl_override.has_value() ? parse_ttl_value(*ttl_override) : std::expected<std::uint32_t, std::string>(0U);
    if (!token_ttl.has_value()) {
      std::cerr << "error: " << token_ttl.error() << "\n";
      return 1;
    }

    const auto idle_timeout = timeout_override.has_value()
                                ? parse_timeout_value(*timeout_override)
                                : std::expected<std::uint32_t, std::string>(30U);
    if (!idle_timeout.has_value()) {
      std::cerr << "error: " << idle_timeout.error() << "\n";
      return 1;
    }
    if (*idle_timeout == 0) {
      std::cerr << "error: session serve timeout must be greater than zero\n";
      return 1;
    }

    if (!ctx.json) {
      std::cout << "Serving " << staged->session_id << " on " << host << ":" << *port
                << " for one client, timeout " << *idle_timeout << "s..." << std::endl;
    }
    if (generate_token && !ctx.json && serve_token->has_value()) {
      print_session_serve_token_hint(host, *port, **serve_token, *token_ttl, allowed_peer.value_or(std::string()));
    }

    const auto served =
      hyprshare::core::serve_staged_file_share(
        *staged,
        host,
        *port,
        ctx.paths,
        ctx.runner,
        serve_token->value_or(std::string()),
        *token_ttl,
        allowed_peer.value_or(std::string()),
        *idle_timeout,
        ctx.tls_config);
    if (!served.has_value()) {
      std::cerr << "error: " << served.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      print_served_share_json(staged->session_id, host, *port, *served);
    } else {
      print_served_share_text(staged->session_id, host, *port, *served);
    }
    return 0;
  }

  if (args[1] == "fetch" && args.size() >= 3) {
    const auto destination_override = take_option(args, "--to");
    const auto token = take_option(args, "--token");
    const bool with_self = take_flag(args, "--with-self");
    const auto endpoint = parse_endpoint_value(args[2]);
    if (!endpoint.has_value()) {
      std::cerr << "error: " << endpoint.error() << "\n";
      return 1;
    }

    std::string peer_id;
    if (with_self) {
      const auto identity = hyprshare::core::load_or_create_device_identity(ctx.paths);
      if (!identity.has_value()) {
        std::cerr << "error: " << identity.error() << "\n";
        return 1;
      }
      peer_id = identity->device_id;
    }

    const auto fetched = hyprshare::core::fetch_staged_file_share(
      endpoint->first,
      endpoint->second,
      ctx.paths,
      ctx.runner,
      token.value_or(std::string()),
      peer_id,
      ctx.tls_config);
    if (!fetched.has_value()) {
      std::cerr << "error: " << fetched.error() << "\n";
      return 1;
    }

    if (destination_override.has_value()) {
      if (!fetched->checksum_path.empty()) {
        const auto verification = hyprshare::core::verify_staged_file_share(*fetched, ctx.runner);
        if (!verification.has_value()) {
          std::cerr << "error: " << verification.error() << "\n";
          return 1;
        }
      }

      const auto received = hyprshare::core::receive_staged_file_share(*fetched, std::filesystem::path(*destination_override));
      if (!received.has_value()) {
        std::cerr << "error: " << received.error() << "\n";
        return 1;
      }

      if (ctx.json) {
        print_received_share_json(*received);
      } else {
        print_received_share_text(*received);
      }
      return 0;
    }

    if (ctx.json) {
      print_staged_share_json(*fetched);
    } else {
      print_staged_share_text(*fetched);
    }
    return 0;
  }

  if (args[1] == "export" && args.size() >= 3) {
    const auto output_override = take_option(args, "--to");
    const auto staged = hyprshare::core::load_staged_file_share(args[2], ctx.paths);
    if (!staged.has_value()) {
      std::cerr << "error: " << staged.error() << "\n";
      return 1;
    }

    const std::filesystem::path bundle_path = output_override.has_value()
                                                ? std::filesystem::path(*output_override)
                                                : std::filesystem::current_path() / (staged->session_id + ".tar.gz");
    const auto exported = hyprshare::core::export_staged_file_share(*staged, bundle_path, ctx.runner);
    if (!exported.has_value()) {
      std::cerr << "error: " << exported.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      print_exported_share_json(staged->session_id, staged->stage_path, *exported);
    } else {
      print_exported_share_text(staged->session_id, staged->stage_path, *exported);
    }
    return 0;
  }

  if (args[1] == "import" && args.size() >= 3) {
    const auto staged = hyprshare::core::import_staged_file_share(args[2], ctx.paths, ctx.runner);
    if (!staged.has_value()) {
      std::cerr << "error: " << staged.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      print_staged_share_json(*staged);
    } else {
      print_staged_share_text(*staged);
    }
    return 0;
  }

  if (args[1] == "receive" && args.size() >= 3) {
    const auto destination_override = take_option(args, "--to");
    const auto staged = hyprshare::core::load_staged_file_share(args[2], ctx.paths);
    if (!staged.has_value()) {
      std::cerr << "error: " << staged.error() << "\n";
      return 1;
    }

    if (!staged->checksum_path.empty()) {
      const auto verification = hyprshare::core::verify_staged_file_share(*staged, ctx.runner);
      if (!verification.has_value()) {
        std::cerr << "error: " << verification.error() << "\n";
        return 1;
      }
    }

    const std::filesystem::path destination_base = destination_override.has_value()
                                                     ? std::filesystem::path(*destination_override)
                                                     : std::filesystem::path(ctx.config.files.receive_dir);
    const auto received = hyprshare::core::receive_staged_file_share(*staged, destination_base);
    if (!received.has_value()) {
      std::cerr << "error: " << received.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      print_received_share_json(*received);
    } else {
      print_received_share_text(*received);
    }
    return 0;
  }

  return std::unexpected(std::string("unknown session subcommand: " + args[1]));
}

}  // namespace hyprshare::cli
