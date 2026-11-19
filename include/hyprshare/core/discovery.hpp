#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "hyprshare/platform/process.hpp"

namespace hyprshare::core {

struct DiscoveryStatus {
  bool avahi_publish_service_available = false;
  bool avahi_browse_available = false;
  bool avahi_daemon_active = false;
  bool ready_to_announce = false;
  bool ready_to_browse = false;
};

struct InboxAnnouncePlan {
  std::string name;
  std::string service_type;
  std::uint16_t port = 0;
  bool token_required = false;
  std::vector<std::string> txt_records;
  std::string command;
};

struct StartedInboxAnnounce {
  InboxAnnouncePlan plan;
  int pid = -1;
};

struct DiscoveredInbox {
  std::string name;
  std::string service_type;
  std::string domain;
  std::string host;
  std::string address;
  std::uint16_t port = 0;
  std::string interface_name;
  std::string protocol;
  bool token_required = false;
  std::vector<std::string> txt_records;
};

DiscoveryStatus inspect_discovery_status(const platform::ProcessRunner& runner);
InboxAnnouncePlan plan_inbox_announce(
  std::string_view name,
  std::uint16_t port,
  bool token_required);
std::expected<StartedInboxAnnounce, std::string> start_inbox_announce(
  const InboxAnnouncePlan& plan,
  const platform::ProcessRunner& runner);
std::expected<std::vector<DiscoveredInbox>, std::string> parse_avahi_browse_output(
  std::string_view output);
std::expected<std::vector<DiscoveredInbox>, std::string> browse_inbox_announcements(
  const platform::ProcessRunner& runner);
std::expected<DiscoveredInbox, std::string> select_discovered_inbox(
  const std::vector<DiscoveredInbox>& inboxes,
  std::string_view name);

}  // namespace hyprshare::core
