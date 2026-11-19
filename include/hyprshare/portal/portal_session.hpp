#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hyprshare::portal {

struct ScreenSource {
  std::string id;
  std::string description;
  bool is_monitor = false;
  bool is_window = false;
};

enum class PortalStatus {
  Idle,
  CreatingSession,
  SelectingSources,
  Started,
  Failed,
};

using StatusCallback = std::function<void(PortalStatus, const std::string&)>;

class PortalSession {
 public:
  PortalSession();
  ~PortalSession();

  PortalSession(const PortalSession&) = delete;
  PortalSession& operator=(const PortalSession&) = delete;

  std::expected<void, std::string> create_session();
  std::expected<void, std::string> select_sources(const std::vector<std::string>& source_ids);
  std::expected<void, std::string> start();
  std::expected<uint32_t, std::string> get_pipewire_node_id() const;

  std::expected<std::vector<ScreenSource>, std::string> list_sources() const;

  void set_status_callback(StatusCallback callback);
  PortalStatus status() const;
  std::string error() const;

  void close();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hyprshare::portal
