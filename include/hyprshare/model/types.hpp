#pragma once

#include <map>
#include <optional>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hyprshare::model {

enum class CapabilityKind {
  Screen,
  Window,
  File,
  Folder,
  Clipboard,
  Camera,
  Network,
};

enum class SupportTier {
  Primary,
  Supported,
  BestEffort,
  Experimental,
};

enum class Severity {
  Info,
  Warning,
  Error,
};

enum class CheckStatus {
  Pass,
  Warn,
  Fail,
  Skip,
};

enum class LaunchActionKind {
  StartHelper,
  LaunchApp,
};

enum class PeerInviteKind {
  Request,
  Response,
};

struct Strategy {
  std::string backend_id;
  std::vector<std::string> conditions;
  std::string expose_as;
};

struct BackendDescriptor {
  std::string id;
  CapabilityKind capability = CapabilityKind::Screen;
  SupportTier support_tier = SupportTier::Supported;
  std::string summary;
  std::vector<std::string> prerequisites;
  bool optional = false;
  bool launch_supported = false;
};

struct AppProfile {
  std::string id;
  std::string display_name;
  std::vector<std::string> matches;
  std::vector<std::string> notes;
  std::vector<std::string> launch_commands;
  std::vector<std::string> launch_env;
  std::vector<Strategy> screen_strategies;
};

using FactMap = std::map<std::string, bool>;

struct CheckResult {
  std::string id;
  CheckStatus status = CheckStatus::Skip;
  Severity severity = Severity::Info;
  std::string summary;
  std::vector<std::string> evidence;
  std::vector<std::string> remediation;
  bool requires_privilege = false;
};

struct DoctorReport {
  FactMap facts;
  std::vector<CheckResult> checks;
};

struct PlanCandidate {
  BackendDescriptor backend;
  std::vector<std::string> unmet_conditions;
  double score = 0.0;
  bool viable = false;
};

struct SharePlan {
  CapabilityKind capability = CapabilityKind::Screen;
  std::string app_hint;
  std::string selected_backend_id;
  std::vector<PlanCandidate> candidates;
  std::vector<std::string> notes;
};

struct LaunchAction {
  LaunchActionKind kind = LaunchActionKind::LaunchApp;
  std::string description;
  std::string command;
};

struct AppLaunchPlan {
  std::string app_hint;
  std::string profile_id;
  std::string display_name;
  std::string resolved_command;
  std::string selected_backend_id;
  bool ready = false;
  std::vector<std::string> env_overrides;
  std::vector<LaunchAction> actions;
  std::vector<std::string> notes;
};

struct DeviceIdentity {
  std::string device_id;
  std::string device_label;
  std::string cert_fingerprint;
  std::int64_t created_at_unix = 0;
};

struct TrustedPeer {
  std::string device_id;
  std::string device_label;
  std::string cert_fingerprint;
  std::int64_t added_at_unix = 0;
};

struct ClipboardText {
  std::string text;
  std::string backend;
  std::size_t byte_count = 0;
  std::int64_t captured_at_unix = 0;
};

struct PeerInvite {
  PeerInviteKind kind = PeerInviteKind::Request;
  std::string from_device_id;
  std::string from_device_label;
  std::string target_device_id;
  std::int64_t created_at_unix = 0;
};

struct FileShareEntry {
  std::string relative_path;
  std::string absolute_path;
  bool is_directory = false;
  std::uintmax_t size_bytes = 0;
};

struct FileShareManifest {
  CapabilityKind capability = CapabilityKind::File;
  std::string share_name;
  std::string root_path;
  bool read_only = true;
  std::uintmax_t total_bytes = 0;
  std::size_t file_count = 0;
  std::size_t directory_count = 0;
  std::vector<FileShareEntry> entries;
  std::vector<std::string> notes;
};

struct StagedFileShare {
  std::string session_id;
  std::string stage_path;
  std::string manifest_path;
  std::string metadata_path;
  std::string payload_path;
  std::string checksum_path;
  std::size_t copied_file_count = 0;
  std::uintmax_t copied_bytes = 0;
  FileShareManifest manifest;
};

struct RemoteFileShareOffer {
  std::string session_id;
  std::string host;
  std::uint16_t port = 0;
  std::uintmax_t bundle_size = 0;
  std::string sender_device_id;
  std::string sender_device_label;
  std::string allowed_peer_id;
  bool auth_required = false;
  std::int64_t auth_expires_at_unix = 0;
  FileShareManifest manifest;
};

struct ReceivedFileShare {
  std::string session_id;
  std::string destination_path;
  std::size_t copied_file_count = 0;
  std::uintmax_t copied_bytes = 0;
  FileShareManifest manifest;
};

struct InboxReceivedFile {
  std::string file_name;
  std::string destination_path;
  std::uintmax_t bytes_received = 0;
  std::string sha256;
};

struct InboxHistoryEntry {
  std::string entry_id;
  std::string file_name;
  std::string destination_path;
  std::uintmax_t bytes_received = 0;
  std::string sha256;
  std::int64_t received_at_unix = 0;
};

std::string to_string(CapabilityKind value);
std::string to_string(SupportTier value);
std::string to_string(Severity value);
std::string to_string(CheckStatus value);
std::string to_string(LaunchActionKind value);
std::string to_string(PeerInviteKind value);

std::optional<CapabilityKind> parse_capability(std::string_view value);
std::optional<SupportTier> parse_support_tier(std::string_view value);
std::optional<PeerInviteKind> parse_peer_invite_kind(std::string_view value);

}  // namespace hyprshare::model
