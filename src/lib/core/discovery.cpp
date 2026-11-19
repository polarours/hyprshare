#include "hyprshare/core/discovery.hpp"

#include <charconv>
#include <sstream>
#include <system_error>

#include "hyprshare/util/string.hpp"

namespace hyprshare::core {

namespace {

bool system_or_process_active(
  const platform::ProcessRunner& runner,
  const std::string_view systemd_unit,
  const std::string_view process_name) {
  if (runner.command_exists("systemctl")) {
    const auto result = runner.run_shell("systemctl is-active " + std::string(systemd_unit));
    if (result.exit_code == 0 && util::trim(result.output) == "active") {
      return true;
    }
  }

  if (runner.command_exists("pidof")) {
    const auto result = runner.run_shell("pidof " + std::string(process_name));
    return result.exit_code == 0 && !util::trim(result.output).empty();
  }
  return false;
}

std::string default_announce_name(const std::string_view name) {
  if (!name.empty()) {
    return std::string(name);
  }
  return "hyprshare inbox";
}

std::string announce_command(
  const std::string_view name,
  const std::string_view service_type,
  const std::uint16_t port,
  const std::vector<std::string>& txt_records) {
  std::ostringstream stream;
  stream << "avahi-publish-service "
         << util::shell_quote(name) << " "
         << util::shell_quote(service_type) << " "
         << port;
  for (const auto& record : txt_records) {
    stream << " " << util::shell_quote(record);
  }
  return stream.str();
}

std::string strip_txt_quotes(const std::string_view value) {
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    return std::string(value.substr(1, value.size() - 2));
  }
  return std::string(value);
}

std::expected<std::uint16_t, std::string> parse_port(const std::string_view value) {
  unsigned int parsed = 0;
  const auto* begin = value.data();
  const auto* end = value.data() + value.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end || parsed > 65535U) {
    return std::unexpected("invalid discovered inbox port: " + std::string(value));
  }
  return static_cast<std::uint16_t>(parsed);
}

}  // namespace

DiscoveryStatus inspect_discovery_status(const platform::ProcessRunner& runner) {
  DiscoveryStatus status;
  status.avahi_publish_service_available = runner.command_exists("avahi-publish-service");
  status.avahi_browse_available = runner.command_exists("avahi-browse");
  status.avahi_daemon_active = system_or_process_active(runner, "avahi-daemon.service", "avahi-daemon");
  status.ready_to_announce = status.avahi_publish_service_available && status.avahi_daemon_active;
  status.ready_to_browse = status.avahi_browse_available && status.avahi_daemon_active;
  return status;
}

InboxAnnouncePlan plan_inbox_announce(
  const std::string_view name,
  const std::uint16_t port,
  const bool token_required) {
  InboxAnnouncePlan plan;
  plan.name = default_announce_name(name);
  plan.service_type = "_hyprshare._tcp";
  plan.port = port;
  plan.token_required = token_required;
  plan.txt_records = {
    "version=1",
    "capability=inbox-put",
    "protocol=http-put",
    "path=/",
    token_required ? "token=required" : "token=none",
  };
  plan.command = announce_command(plan.name, plan.service_type, plan.port, plan.txt_records);
  return plan;
}

std::expected<StartedInboxAnnounce, std::string> start_inbox_announce(
  const InboxAnnouncePlan& plan,
  const platform::ProcessRunner& runner) {
  const auto status = inspect_discovery_status(runner);
  if (!status.avahi_publish_service_available) {
    return std::unexpected("avahi-publish-service is not available; install Avahi tools to announce inbox receivers");
  }
  if (!status.avahi_daemon_active) {
    return std::unexpected("avahi-daemon is not active; start avahi-daemon.service before announcing inbox receivers");
  }

  const auto pid = runner.spawn_detached_shell(plan.command);
  if (!pid.has_value()) {
    return std::unexpected(pid.error());
  }

  StartedInboxAnnounce started;
  started.plan = plan;
  started.pid = *pid;
  return started;
}

std::expected<std::vector<DiscoveredInbox>, std::string> parse_avahi_browse_output(
  const std::string_view output) {
  std::vector<DiscoveredInbox> discovered;
  std::istringstream stream{std::string(output)};
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty() || line.front() != '=') {
      continue;
    }

    const auto fields = util::split(line, ';');
    if (fields.size() < 9) {
      continue;
    }

    const auto port = parse_port(fields[8]);
    if (!port.has_value()) {
      return std::unexpected(port.error());
    }

    DiscoveredInbox inbox;
    inbox.interface_name = fields[1];
    inbox.protocol = fields[2];
    inbox.name = fields[3];
    inbox.service_type = fields[4];
    inbox.domain = fields[5];
    inbox.host = fields[6];
    inbox.address = fields[7];
    inbox.port = *port;
    for (std::size_t index = 9; index < fields.size(); ++index) {
      const std::string record = strip_txt_quotes(fields[index]);
      if (record.empty()) {
        continue;
      }
      if (record == "token=required") {
        inbox.token_required = true;
      }
      inbox.txt_records.push_back(record);
    }
    discovered.push_back(inbox);
  }
  return discovered;
}

std::expected<std::vector<DiscoveredInbox>, std::string> browse_inbox_announcements(
  const platform::ProcessRunner& runner) {
  const auto status = inspect_discovery_status(runner);
  if (!status.avahi_browse_available) {
    return std::unexpected("avahi-browse is not available; install Avahi tools to browse inbox receivers");
  }
  if (!status.avahi_daemon_active) {
    return std::unexpected("avahi-daemon is not active; start avahi-daemon.service before browsing inbox receivers");
  }

  const auto result = runner.run_shell("avahi-browse -prt _hyprshare._tcp");
  if (result.exit_code != 0) {
    return std::unexpected("avahi-browse failed: " + result.output);
  }
  return parse_avahi_browse_output(result.output);
}

std::expected<DiscoveredInbox, std::string> select_discovered_inbox(
  const std::vector<DiscoveredInbox>& inboxes,
  const std::string_view name) {
  std::vector<DiscoveredInbox> matches;
  for (const auto& inbox : inboxes) {
    if (inbox.name == name) {
      matches.push_back(inbox);
    }
  }

  if (matches.empty()) {
    return std::unexpected("no discovered hyprshare inbox matched `" + std::string(name) + "`");
  }
  if (matches.size() > 1) {
    return std::unexpected("multiple discovered hyprshare inbox receivers matched `" + std::string(name) + "`");
  }
  return matches.front();
}

}  // namespace hyprshare::core
