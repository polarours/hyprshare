#include "cmd_peer.hpp"

#include <filesystem>
#include <iostream>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/core/trust_store.hpp"

namespace hyprshare::cli {

using hyprshare::model::DeviceIdentity;
using hyprshare::model::PeerInvite;
using hyprshare::model::TrustedPeer;

std::expected<int, std::string> handle_peer(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string("unknown peer subcommand"));
  }

  if (args[1] == "self") {
    const auto identity = hyprshare::core::load_or_create_device_identity(ctx.paths);
    if (!identity.has_value()) {
      std::cerr << "error: " << identity.error() << "\n";
      return 1;
    }
    if (ctx.json) {
      print_device_identity_json(*identity);
    } else {
      print_device_identity_text(*identity);
    }
    return 0;
  }

  if (args[1] == "list") {
    const auto peers = hyprshare::core::list_trusted_peers(ctx.paths);
    if (!peers.has_value()) {
      std::cerr << "error: " << peers.error() << "\n";
      return 1;
    }
    if (ctx.json) {
      print_trusted_peers_json(*peers);
    } else {
      print_trusted_peers_text(*peers);
    }
    return 0;
  }

  if (args[1] == "inspect" && args.size() >= 3) {
    const auto identity = hyprshare::core::inspect_peer_card(args[2]);
    if (!identity.has_value()) {
      std::cerr << "error: " << identity.error() << "\n";
      return 1;
    }
    if (ctx.json) {
      print_device_identity_json(*identity);
    } else {
      print_device_identity_text(*identity);
    }
    return 0;
  }

  if (args[1] == "pair" && args.size() >= 3) {
    const auto paired = hyprshare::core::import_trusted_peer(args[2], ctx.paths);
    if (!paired.has_value()) {
      std::cerr << "error: " << paired.error() << "\n";
      return 1;
    }
    if (ctx.json) {
      print_trusted_peer_json(*paired);
    } else {
      print_trusted_peer_text(*paired);
    }
    return 0;
  }

  if (args[1] == "invite" && args.size() >= 3 && args[2] == "create") {
    const auto output_override = take_option(args, "--to");
    const auto identity = hyprshare::core::load_or_create_device_identity(ctx.paths);
    if (!identity.has_value()) {
      std::cerr << "error: " << identity.error() << "\n";
      return 1;
    }

    const std::filesystem::path output_path = output_override.has_value()
                                                ? std::filesystem::path(*output_override)
                                                : std::filesystem::current_path() / (identity->device_id + ".request.invite");
    const auto created = hyprshare::core::create_peer_invite(ctx.paths, output_path);
    if (!created.has_value()) {
      std::cerr << "error: " << created.error() << "\n";
      return 1;
    }

    const auto inspected = hyprshare::core::inspect_peer_invite(*created);
    if (!inspected.has_value()) {
      std::cerr << "error: " << inspected.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      print_written_peer_invite_json(*inspected, *created);
    } else {
      print_written_peer_invite_text(*inspected, *created);
    }
    return 0;
  }

  if (args[1] == "invite" && args.size() >= 4 && args[2] == "inspect") {
    const auto invite = hyprshare::core::inspect_peer_invite(args[3]);
    if (!invite.has_value()) {
      std::cerr << "error: " << invite.error() << "\n";
      return 1;
    }
    if (ctx.json) {
      print_peer_invite_json(*invite);
    } else {
      print_peer_invite_text(*invite);
    }
    return 0;
  }

  if (args[1] == "invite" && args.size() >= 4 && args[2] == "respond") {
    const auto output_override = take_option(args, "--to");
    const auto request_invite = hyprshare::core::inspect_peer_invite(args[3]);
    if (!request_invite.has_value()) {
      std::cerr << "error: " << request_invite.error() << "\n";
      return 1;
    }

    const std::filesystem::path output_path = output_override.has_value()
                                                ? std::filesystem::path(*output_override)
                                                : std::filesystem::current_path() / (request_invite->from_device_id + ".response.invite");
    const auto responded = hyprshare::core::respond_to_peer_invite(args[3], ctx.paths, output_path);
    if (!responded.has_value()) {
      std::cerr << "error: " << responded.error() << "\n";
      return 1;
    }

    const auto response_invite = hyprshare::core::inspect_peer_invite(*responded);
    if (!response_invite.has_value()) {
      std::cerr << "error: " << response_invite.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      print_written_peer_invite_json(*response_invite, *responded);
    } else {
      print_written_peer_invite_text(*response_invite, *responded);
    }
    return 0;
  }

  if (args[1] == "invite" && args.size() >= 4 && args[2] == "complete") {
    const auto paired = hyprshare::core::complete_peer_invite(args[3], ctx.paths);
    if (!paired.has_value()) {
      std::cerr << "error: " << paired.error() << "\n";
      return 1;
    }
    if (ctx.json) {
      print_trusted_peer_json(*paired);
    } else {
      print_trusted_peer_text(*paired);
    }
    return 0;
  }

  if (args[1] == "export") {
    const auto output_override = take_option(args, "--to");
    const auto identity = hyprshare::core::load_or_create_device_identity(ctx.paths);
    if (!identity.has_value()) {
      std::cerr << "error: " << identity.error() << "\n";
      return 1;
    }

    const std::filesystem::path output_path = output_override.has_value()
                                                ? std::filesystem::path(*output_override)
                                                : std::filesystem::current_path() / (identity->device_id + ".peer");
    const auto exported = hyprshare::core::export_device_identity(ctx.paths, output_path);
    if (!exported.has_value()) {
      std::cerr << "error: " << exported.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      print_exported_peer_json(*identity, *exported);
    } else {
      print_exported_peer_text(*identity, *exported);
    }
    return 0;
  }

  if (args[1] == "import" && args.size() >= 3) {
    const auto imported = hyprshare::core::import_trusted_peer(args[2], ctx.paths);
    if (!imported.has_value()) {
      std::cerr << "error: " << imported.error() << "\n";
      return 1;
    }
    if (ctx.json) {
      print_trusted_peer_json(*imported);
    } else {
      print_trusted_peer_text(*imported);
    }
    return 0;
  }

  if (args[1] == "add" && args.size() >= 3) {
    const auto label = take_option(args, "--label");
    const auto peer = hyprshare::core::add_trusted_peer(args[2], label.value_or(args[2]), ctx.paths);
    if (!peer.has_value()) {
      std::cerr << "error: " << peer.error() << "\n";
      return 1;
    }
    if (ctx.json) {
      print_trusted_peer_json(*peer);
    } else {
      print_trusted_peer_text(*peer);
    }
    return 0;
  }

  if (args[1] == "remove" && args.size() >= 3) {
    const auto removed = hyprshare::core::remove_trusted_peer(args[2], ctx.paths);
    if (!removed.has_value()) {
      std::cerr << "error: " << removed.error() << "\n";
      return 1;
    }
    if (ctx.json) {
      print_removed_peer_json(args[2], *removed);
    } else {
      print_removed_peer_text(args[2], *removed);
    }
    return 0;
  }

  return std::unexpected(std::string("unknown peer subcommand: " + args[1]));
}

}  // namespace hyprshare::cli
