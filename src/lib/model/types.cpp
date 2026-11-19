#include "hyprshare/model/types.hpp"

#include <array>

#include "hyprshare/util/string.hpp"

namespace hyprshare::model {

namespace {

template <typename EnumType>
std::optional<EnumType> parse_enum(
  std::string_view value,
  const std::initializer_list<std::pair<std::string_view, EnumType>>& mapping) {
  const std::string lower = util::to_lower(value);
  for (const auto& [name, enum_value] : mapping) {
    if (lower == name) {
      return enum_value;
    }
  }
  return std::nullopt;
}

}  // namespace

std::string to_string(const CapabilityKind value) {
  switch (value) {
    case CapabilityKind::Screen:
      return "screen";
    case CapabilityKind::Window:
      return "window";
    case CapabilityKind::File:
      return "file";
    case CapabilityKind::Folder:
      return "folder";
    case CapabilityKind::Clipboard:
      return "clipboard";
    case CapabilityKind::Camera:
      return "camera";
    case CapabilityKind::Network:
      return "network";
  }
  return "unknown";
}

std::string to_string(const SupportTier value) {
  switch (value) {
    case SupportTier::Primary:
      return "primary";
    case SupportTier::Supported:
      return "supported";
    case SupportTier::BestEffort:
      return "best_effort";
    case SupportTier::Experimental:
      return "experimental";
  }
  return "unknown";
}

std::string to_string(const Severity value) {
  switch (value) {
    case Severity::Info:
      return "info";
    case Severity::Warning:
      return "warning";
    case Severity::Error:
      return "error";
  }
  return "unknown";
}

std::string to_string(const CheckStatus value) {
  switch (value) {
    case CheckStatus::Pass:
      return "pass";
    case CheckStatus::Warn:
      return "warn";
    case CheckStatus::Fail:
      return "fail";
    case CheckStatus::Skip:
      return "skip";
  }
  return "unknown";
}

std::string to_string(const LaunchActionKind value) {
  switch (value) {
    case LaunchActionKind::StartHelper:
      return "start_helper";
    case LaunchActionKind::LaunchApp:
      return "launch_app";
  }
  return "unknown";
}

std::string to_string(const PeerInviteKind value) {
  switch (value) {
    case PeerInviteKind::Request:
      return "request";
    case PeerInviteKind::Response:
      return "response";
  }
  return "unknown";
}

std::optional<CapabilityKind> parse_capability(const std::string_view value) {
  return parse_enum<CapabilityKind>(
    value,
    {
      {"screen", CapabilityKind::Screen},
      {"window", CapabilityKind::Window},
      {"file", CapabilityKind::File},
      {"folder", CapabilityKind::Folder},
      {"clipboard", CapabilityKind::Clipboard},
      {"camera", CapabilityKind::Camera},
      {"network", CapabilityKind::Network},
    });
}

std::optional<SupportTier> parse_support_tier(const std::string_view value) {
  return parse_enum<SupportTier>(
    value,
    {
      {"primary", SupportTier::Primary},
      {"supported", SupportTier::Supported},
      {"best_effort", SupportTier::BestEffort},
      {"experimental", SupportTier::Experimental},
    });
}

std::optional<PeerInviteKind> parse_peer_invite_kind(const std::string_view value) {
  return parse_enum<PeerInviteKind>(
    value,
    {
      {"request", PeerInviteKind::Request},
      {"response", PeerInviteKind::Response},
    });
}

}  // namespace hyprshare::model
