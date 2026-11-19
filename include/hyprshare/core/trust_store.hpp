#pragma once

#include <expected>
#include <filesystem>
#include <string_view>
#include <vector>

#include "hyprshare/model/types.hpp"
#include "hyprshare/platform/paths.hpp"

namespace hyprshare::core {

std::expected<model::DeviceIdentity, std::string> load_or_create_device_identity(
  const platform::Paths& paths);
std::expected<std::vector<model::TrustedPeer>, std::string> list_trusted_peers(
  const platform::Paths& paths);
std::expected<model::DeviceIdentity, std::string> inspect_peer_card(
  const std::filesystem::path& input_path);
std::expected<model::PeerInvite, std::string> inspect_peer_invite(
  const std::filesystem::path& input_path);
std::expected<std::filesystem::path, std::string> export_device_identity(
  const platform::Paths& paths,
  const std::filesystem::path& output_path);
std::expected<std::filesystem::path, std::string> create_peer_invite(
  const platform::Paths& paths,
  const std::filesystem::path& output_path);
std::expected<std::filesystem::path, std::string> respond_to_peer_invite(
  const std::filesystem::path& input_path,
  const platform::Paths& paths,
  const std::filesystem::path& output_path);
std::expected<model::TrustedPeer, std::string> complete_peer_invite(
  const std::filesystem::path& input_path,
  const platform::Paths& paths);
std::expected<model::TrustedPeer, std::string> import_trusted_peer(
  const std::filesystem::path& input_path,
  const platform::Paths& paths);
std::expected<model::TrustedPeer, std::string> add_trusted_peer(
  std::string_view device_id,
  std::string_view device_label,
  const platform::Paths& paths);
std::expected<std::filesystem::path, std::string> remove_trusted_peer(
  std::string_view device_id,
  const platform::Paths& paths);

}  // namespace hyprshare::core
