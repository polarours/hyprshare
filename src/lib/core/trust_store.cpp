#include "hyprshare/core/trust_store.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>

#include "hyprshare/util/string.hpp"

namespace hyprshare::core {

namespace {

std::filesystem::path trust_root(const platform::Paths& paths) { return paths.state_dir / "trust"; }

std::filesystem::path self_identity_path(const platform::Paths& paths) { return trust_root(paths) / "self.meta"; }

std::filesystem::path trusted_peers_root(const platform::Paths& paths) { return trust_root(paths) / "peers"; }

std::filesystem::path trusted_peer_path(const platform::Paths& paths, const std::string_view device_id) {
  return trusted_peers_root(paths) / (std::string(device_id) + ".meta");
}

std::int64_t unix_time_now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
           std::chrono::system_clock::now().time_since_epoch())
    .count();
}

bool is_valid_device_id(const std::string_view device_id) {
  if (device_id.empty()) {
    return false;
  }

  return std::all_of(device_id.begin(), device_id.end(), [](const unsigned char ch) {
    return std::isalnum(ch) != 0 || ch == '-' || ch == '_';
  });
}

bool is_valid_device_label(const std::string_view label) {
  if (label.empty()) {
    return false;
  }

  return std::all_of(label.begin(), label.end(), [](const char ch) {
    return ch != '\n' && ch != '\r' && ch != '=';
  });
}

std::string default_device_label() {
  std::array<char, 256> hostname{};
  if (gethostname(hostname.data(), hostname.size()) == 0) {
    hostname.back() = '\0';
    const std::string value(hostname.data());
    if (!value.empty() && is_valid_device_label(value)) {
      return value;
    }
  }
  return "hyprshare-device";
}

std::expected<std::string, std::string> random_device_id() {
  std::ifstream input("/dev/urandom", std::ios::binary);
  if (!input.is_open()) {
    return std::unexpected("failed to open /dev/urandom for device identity generation");
  }

  std::array<unsigned char, 16> bytes{};
  input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
    return std::unexpected("failed to read random bytes for device identity generation");
  }

  constexpr char hex_digits[] = "0123456789abcdef";
  std::string device_id;
  device_id.reserve(bytes.size() * 2);
  for (const auto byte : bytes) {
    device_id.push_back(hex_digits[(byte >> 4) & 0x0F]);
    device_id.push_back(hex_digits[byte & 0x0F]);
  }
  return device_id;
}

std::expected<void, std::string> ensure_directory(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::create_directories(path, error);
  if (error) {
    return std::unexpected("failed to create directory `" + path.string() + "`: " + error.message());
  }
  return {};
}

std::expected<std::filesystem::path, std::string> normalize_existing_path(const std::filesystem::path& path) {
  if (path.empty()) {
    return std::unexpected("path must not be empty");
  }

  std::error_code error;
  const auto absolute = std::filesystem::absolute(path, error);
  if (error) {
    return std::unexpected("failed to resolve path `" + path.string() + "`: " + error.message());
  }

  if (!std::filesystem::exists(absolute)) {
    return std::unexpected("path does not exist: " + absolute.string());
  }

  return absolute;
}

std::expected<std::map<std::string, std::string>, std::string> read_metadata_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    return std::unexpected("failed to open metadata file: " + path.string());
  }

  std::map<std::string, std::string> values;
  std::string line;
  while (std::getline(input, line)) {
    const auto separator = line.find('=');
    if (separator == std::string::npos) {
      continue;
    }
    values.emplace(line.substr(0, separator), line.substr(separator + 1));
  }
  return values;
}

std::expected<void, std::string> write_device_identity_file(
  const model::DeviceIdentity& identity,
  const std::filesystem::path& path) {
  std::ofstream output(path);
  if (!output.is_open()) {
    return std::unexpected("failed to open device identity file for writing: " + path.string());
  }

  output << "device_id=" << identity.device_id << "\n";
  output << "device_label=" << identity.device_label << "\n";
  output << "created_at_unix=" << identity.created_at_unix << "\n";
  return {};
}

std::expected<void, std::string> write_trusted_peer_file(
  const model::TrustedPeer& peer,
  const std::filesystem::path& path) {
  std::ofstream output(path);
  if (!output.is_open()) {
    return std::unexpected("failed to open trusted peer file for writing: " + path.string());
  }

  output << "device_id=" << peer.device_id << "\n";
  output << "device_label=" << peer.device_label << "\n";
  output << "added_at_unix=" << peer.added_at_unix << "\n";
  return {};
}

std::expected<void, std::string> write_peer_invite_file(
  const model::PeerInvite& invite,
  const std::filesystem::path& path) {
  std::ofstream output(path);
  if (!output.is_open()) {
    return std::unexpected("failed to open peer invite file for writing: " + path.string());
  }

  output << "invite_kind=" << model::to_string(invite.kind) << "\n";
  output << "from_device_id=" << invite.from_device_id << "\n";
  output << "from_device_label=" << invite.from_device_label << "\n";
  output << "target_device_id=" << invite.target_device_id << "\n";
  output << "created_at_unix=" << invite.created_at_unix << "\n";
  return {};
}

std::expected<model::DeviceIdentity, std::string> load_device_identity_file(const std::filesystem::path& path) {
  const auto values = read_metadata_file(path);
  if (!values.has_value()) {
    return std::unexpected(values.error());
  }

  model::DeviceIdentity identity;
  const auto id_it = values->find("device_id");
  const auto label_it = values->find("device_label");
  const auto created_it = values->find("created_at_unix");
  if (id_it == values->end() || label_it == values->end() || created_it == values->end()) {
    return std::unexpected("device identity file is missing required fields: " + path.string());
  }

  if (!is_valid_device_id(id_it->second)) {
    return std::unexpected("device identity file contains an invalid device id: " + path.string());
  }
  if (!is_valid_device_label(label_it->second)) {
    return std::unexpected("device identity file contains an invalid device label: " + path.string());
  }

  try {
    identity.device_id = id_it->second;
    identity.device_label = label_it->second;
    identity.created_at_unix = std::stoll(created_it->second);
  } catch (const std::exception&) {
    return std::unexpected("device identity file contains an invalid numeric field: " + path.string());
  }
  return identity;
}

std::expected<model::TrustedPeer, std::string> load_trusted_peer_file(const std::filesystem::path& path) {
  const auto values = read_metadata_file(path);
  if (!values.has_value()) {
    return std::unexpected(values.error());
  }

  model::TrustedPeer peer;
  const auto id_it = values->find("device_id");
  const auto label_it = values->find("device_label");
  const auto added_it = values->find("added_at_unix");
  if (id_it == values->end() || label_it == values->end() || added_it == values->end()) {
    return std::unexpected("trusted peer file is missing required fields: " + path.string());
  }

  if (!is_valid_device_id(id_it->second)) {
    return std::unexpected("trusted peer file contains an invalid device id: " + path.string());
  }
  if (!is_valid_device_label(label_it->second)) {
    return std::unexpected("trusted peer file contains an invalid device label: " + path.string());
  }

  try {
    peer.device_id = id_it->second;
    peer.device_label = label_it->second;
    peer.added_at_unix = std::stoll(added_it->second);
  } catch (const std::exception&) {
    return std::unexpected("trusted peer file contains an invalid numeric field: " + path.string());
  }
  return peer;
}

std::expected<model::PeerInvite, std::string> load_peer_invite_file(const std::filesystem::path& path) {
  const auto values = read_metadata_file(path);
  if (!values.has_value()) {
    return std::unexpected(values.error());
  }

  model::PeerInvite invite;
  const auto kind_it = values->find("invite_kind");
  const auto id_it = values->find("from_device_id");
  const auto label_it = values->find("from_device_label");
  const auto target_it = values->find("target_device_id");
  const auto created_it = values->find("created_at_unix");
  if (kind_it == values->end() || id_it == values->end() || label_it == values->end() ||
      target_it == values->end() || created_it == values->end()) {
    return std::unexpected("peer invite file is missing required fields: " + path.string());
  }

  const auto invite_kind = model::parse_peer_invite_kind(kind_it->second);
  if (!invite_kind.has_value()) {
    return std::unexpected("peer invite file contains an invalid invite kind: " + path.string());
  }
  if (!is_valid_device_id(id_it->second)) {
    return std::unexpected("peer invite file contains an invalid source device id: " + path.string());
  }
  if (!is_valid_device_label(label_it->second)) {
    return std::unexpected("peer invite file contains an invalid source device label: " + path.string());
  }

  if (*invite_kind == model::PeerInviteKind::Request) {
    if (!target_it->second.empty()) {
      return std::unexpected("peer invite request must not declare a target device id: " + path.string());
    }
  } else if (!is_valid_device_id(target_it->second)) {
    return std::unexpected("peer invite response must declare a valid target device id: " + path.string());
  }

  try {
    invite.kind = *invite_kind;
    invite.from_device_id = id_it->second;
    invite.from_device_label = label_it->second;
    invite.target_device_id = target_it->second;
    invite.created_at_unix = std::stoll(created_it->second);
  } catch (const std::exception&) {
    return std::unexpected("peer invite file contains an invalid numeric field: " + path.string());
  }
  return invite;
}

}  // namespace

std::expected<model::DeviceIdentity, std::string> load_or_create_device_identity(const platform::Paths& paths) {
  const auto identity_path = self_identity_path(paths);
  if (std::filesystem::exists(identity_path)) {
    return load_device_identity_file(identity_path);
  }

  auto directory_result = ensure_directory(trust_root(paths));
  if (!directory_result.has_value()) {
    return std::unexpected(directory_result.error());
  }

  auto device_id = random_device_id();
  if (!device_id.has_value()) {
    return std::unexpected(device_id.error());
  }

  model::DeviceIdentity identity;
  identity.device_id = *device_id;
  identity.device_label = default_device_label();
  identity.created_at_unix = unix_time_now();

  auto write_result = write_device_identity_file(identity, identity_path);
  if (!write_result.has_value()) {
    return std::unexpected(write_result.error());
  }
  return identity;
}

std::expected<std::vector<model::TrustedPeer>, std::string> list_trusted_peers(const platform::Paths& paths) {
  const auto peers_root = trusted_peers_root(paths);
  if (!std::filesystem::exists(peers_root)) {
    return std::vector<model::TrustedPeer>{};
  }

  std::vector<model::TrustedPeer> peers;
  for (const auto& entry : std::filesystem::directory_iterator(peers_root)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    auto peer = load_trusted_peer_file(entry.path());
    if (!peer.has_value()) {
      return std::unexpected(peer.error());
    }
    peers.push_back(*peer);
  }

  std::sort(peers.begin(), peers.end(), [](const model::TrustedPeer& left, const model::TrustedPeer& right) {
    return left.device_id < right.device_id;
  });
  return peers;
}

std::expected<model::DeviceIdentity, std::string> inspect_peer_card(const std::filesystem::path& input_path) {
  const auto normalized = normalize_existing_path(input_path);
  if (!normalized.has_value()) {
    return std::unexpected(normalized.error());
  }

  if (!std::filesystem::is_regular_file(*normalized)) {
    return std::unexpected("peer card path is not a regular file: " + normalized->string());
  }

  return load_device_identity_file(*normalized);
}

std::expected<model::PeerInvite, std::string> inspect_peer_invite(const std::filesystem::path& input_path) {
  const auto normalized = normalize_existing_path(input_path);
  if (!normalized.has_value()) {
    return std::unexpected(normalized.error());
  }

  if (!std::filesystem::is_regular_file(*normalized)) {
    return std::unexpected("peer invite path is not a regular file: " + normalized->string());
  }

  return load_peer_invite_file(*normalized);
}

std::expected<std::filesystem::path, std::string> export_device_identity(
  const platform::Paths& paths,
  const std::filesystem::path& output_path) {
  if (output_path.empty()) {
    return std::unexpected("peer export path must not be empty");
  }

  const auto identity = load_or_create_device_identity(paths);
  if (!identity.has_value()) {
    return std::unexpected(identity.error());
  }

  std::error_code error;
  const auto absolute_path = std::filesystem::absolute(output_path, error);
  if (error) {
    return std::unexpected("failed to resolve peer export path `" + output_path.string() + "`: " + error.message());
  }

  auto directory_result = ensure_directory(absolute_path.parent_path());
  if (!directory_result.has_value()) {
    return std::unexpected(directory_result.error());
  }

  auto write_result = write_device_identity_file(*identity, absolute_path);
  if (!write_result.has_value()) {
    return std::unexpected(write_result.error());
  }
  return absolute_path;
}

std::expected<std::filesystem::path, std::string> create_peer_invite(
  const platform::Paths& paths,
  const std::filesystem::path& output_path) {
  if (output_path.empty()) {
    return std::unexpected("peer invite create path must not be empty");
  }

  const auto identity = load_or_create_device_identity(paths);
  if (!identity.has_value()) {
    return std::unexpected(identity.error());
  }

  std::error_code error;
  const auto absolute_path = std::filesystem::absolute(output_path, error);
  if (error) {
    return std::unexpected("failed to resolve peer invite create path `" + output_path.string() + "`: " + error.message());
  }

  auto directory_result = ensure_directory(absolute_path.parent_path());
  if (!directory_result.has_value()) {
    return std::unexpected(directory_result.error());
  }

  model::PeerInvite invite;
  invite.kind = model::PeerInviteKind::Request;
  invite.from_device_id = identity->device_id;
  invite.from_device_label = identity->device_label;
  invite.created_at_unix = unix_time_now();

  auto write_result = write_peer_invite_file(invite, absolute_path);
  if (!write_result.has_value()) {
    return std::unexpected(write_result.error());
  }
  return absolute_path;
}

std::expected<std::filesystem::path, std::string> respond_to_peer_invite(
  const std::filesystem::path& input_path,
  const platform::Paths& paths,
  const std::filesystem::path& output_path) {
  if (output_path.empty()) {
    return std::unexpected("peer invite respond path must not be empty");
  }

  const auto invite = inspect_peer_invite(input_path);
  if (!invite.has_value()) {
    return std::unexpected(invite.error());
  }
  if (invite->kind != model::PeerInviteKind::Request) {
    return std::unexpected("peer invite respond requires a request invite");
  }

  const auto self = load_or_create_device_identity(paths);
  if (!self.has_value()) {
    return std::unexpected(self.error());
  }
  if (invite->from_device_id == self->device_id) {
    return std::unexpected("cannot respond to a peer invite created by the local device");
  }

  const auto added_peer = add_trusted_peer(invite->from_device_id, invite->from_device_label, paths);
  if (!added_peer.has_value()) {
    return std::unexpected(added_peer.error());
  }

  std::error_code error;
  const auto absolute_path = std::filesystem::absolute(output_path, error);
  if (error) {
    return std::unexpected("failed to resolve peer invite respond path `" + output_path.string() + "`: " + error.message());
  }

  auto directory_result = ensure_directory(absolute_path.parent_path());
  if (!directory_result.has_value()) {
    return std::unexpected(directory_result.error());
  }

  model::PeerInvite response;
  response.kind = model::PeerInviteKind::Response;
  response.from_device_id = self->device_id;
  response.from_device_label = self->device_label;
  response.target_device_id = invite->from_device_id;
  response.created_at_unix = unix_time_now();

  auto write_result = write_peer_invite_file(response, absolute_path);
  if (!write_result.has_value()) {
    return std::unexpected(write_result.error());
  }
  return absolute_path;
}

std::expected<model::TrustedPeer, std::string> complete_peer_invite(
  const std::filesystem::path& input_path,
  const platform::Paths& paths) {
  const auto invite = inspect_peer_invite(input_path);
  if (!invite.has_value()) {
    return std::unexpected(invite.error());
  }
  if (invite->kind != model::PeerInviteKind::Response) {
    return std::unexpected("peer invite complete requires a response invite");
  }

  const auto self = load_or_create_device_identity(paths);
  if (!self.has_value()) {
    return std::unexpected(self.error());
  }
  if (invite->target_device_id != self->device_id) {
    return std::unexpected("peer invite response is not addressed to the local device");
  }

  return add_trusted_peer(invite->from_device_id, invite->from_device_label, paths);
}

std::expected<model::TrustedPeer, std::string> import_trusted_peer(
  const std::filesystem::path& input_path,
  const platform::Paths& paths) {
  const auto identity = inspect_peer_card(input_path);
  if (!identity.has_value()) {
    return std::unexpected(identity.error());
  }

  return add_trusted_peer(identity->device_id, identity->device_label, paths);
}

std::expected<model::TrustedPeer, std::string> add_trusted_peer(
  const std::string_view device_id,
  const std::string_view device_label,
  const platform::Paths& paths) {
  if (!is_valid_device_id(device_id)) {
    return std::unexpected("trusted peer device id must be non-empty and use only letters, digits, '-' or '_'");
  }
  if (!is_valid_device_label(device_label)) {
    return std::unexpected("trusted peer label must be non-empty and must not contain newlines or '='");
  }

  const auto self = load_or_create_device_identity(paths);
  if (!self.has_value()) {
    return std::unexpected(self.error());
  }
  if (self->device_id == device_id) {
    return std::unexpected("cannot trust the local device as a remote peer");
  }

  auto directory_result = ensure_directory(trusted_peers_root(paths));
  if (!directory_result.has_value()) {
    return std::unexpected(directory_result.error());
  }

  const auto peer_path = trusted_peer_path(paths, device_id);
  if (std::filesystem::exists(peer_path)) {
    return std::unexpected("trusted peer already exists: " + std::string(device_id));
  }

  model::TrustedPeer peer;
  peer.device_id = std::string(device_id);
  peer.device_label = std::string(device_label);
  peer.added_at_unix = unix_time_now();

  auto write_result = write_trusted_peer_file(peer, peer_path);
  if (!write_result.has_value()) {
    return std::unexpected(write_result.error());
  }
  return peer;
}

std::expected<std::filesystem::path, std::string> remove_trusted_peer(
  const std::string_view device_id,
  const platform::Paths& paths) {
  if (!is_valid_device_id(device_id)) {
    return std::unexpected("trusted peer device id must be non-empty and use only letters, digits, '-' or '_'");
  }

  const auto peer_path = trusted_peer_path(paths, device_id);
  if (!std::filesystem::exists(peer_path)) {
    return std::unexpected("trusted peer does not exist: " + std::string(device_id));
  }

  std::error_code error;
  std::filesystem::remove(peer_path, error);
  if (error) {
    return std::unexpected("failed to remove trusted peer `" + peer_path.string() + "`: " + error.message());
  }
  return peer_path;
}

}  // namespace hyprshare::core
