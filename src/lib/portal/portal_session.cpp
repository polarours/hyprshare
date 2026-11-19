#include "hyprshare/portal/portal_session.hpp"

#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/IProxy.h>
#include <sdbus-c++/Types.h>

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

namespace hyprshare::portal {

namespace {

constexpr const char* PORTAL_SERVICE = "org.freedesktop.portal.Desktop";
constexpr const char* SCREENCAST_INTERFACE = "org.freedesktop.portal.ScreenCast";
constexpr const char* REQUEST_INTERFACE = "org.freedesktop.portal.Request";
constexpr const char* OBJECT_PATH = "/org/freedesktop/portal/desktop";

using PortalResults = std::map<std::string, sdbus::Variant>;
using PortalStreams = std::vector<std::tuple<uint32_t, PortalResults>>;

// Portal ScreenCast methods (CreateSession/SelectSources/Start) are asynchronous:
// they return a Request object path immediately and deliver the real result later
// through the org.freedesktop.portal.Request::Response signal.
struct PortalResponse {
  uint32_t code = 2;
  PortalResults results;
};

bool wait_for_response(
    sdbus::IConnection& connection,
    const std::string& request_path,
    PortalResponse& out,
    std::string& out_error,
    const std::chrono::seconds timeout) {
  if (request_path.empty()) {
    out_error = "portal returned an empty request handle";
    return false;
  }

  // The request object lives on the same bus connection, so we subscribe to its
  // Response signal through a dedicated proxy for that object path.
  // NOTE: the connection's event loop must be running for this to fire; the
  // PortalSession constructor starts it with enterEventLoopAsync().
  std::mutex mutex;
  std::condition_variable cv;
  bool done = false;

  try {
    auto request_proxy = sdbus::createProxy(
        connection,
        sdbus::ServiceName{PORTAL_SERVICE},
        sdbus::ObjectPath{request_path});

    request_proxy->uponSignal("Response")
      .onInterface(REQUEST_INTERFACE)
      .call([&](uint32_t code, PortalResults results) {
        {
          std::lock_guard<std::mutex> lock(mutex);
          out.code = code;
          out.results = std::move(results);
          done = true;
        }
        cv.notify_one();
      });

    std::unique_lock<std::mutex> lock(mutex);
    if (!cv.wait_for(lock, timeout, [&] { return done; })) {
      out_error = "timed out waiting for portal response on " + request_path;
      return false;
    }
  } catch (const std::exception& e) {
    out_error = std::string("failed waiting for portal response: ") + e.what();
    return false;
  }

  if (out.code != 0) {
    out_error = "portal request was not successful (response code " + std::to_string(out.code) + ")";
    return false;
  }
  return true;
}

}  // namespace

struct PortalSession::Impl {
  std::unique_ptr<sdbus::IConnection> connection;
  std::unique_ptr<sdbus::IProxy> proxy;
  PortalStatus current_status = PortalStatus::Idle;
  std::string last_error;
  std::string session_handle;
  uint32_t pipewire_node_id = 0;
  StatusCallback callback;
  std::vector<ScreenSource> available_sources;

  void update_status(PortalStatus status, const std::string& error = "") {
    current_status = status;
    last_error = error;
    if (callback) {
      callback(status, error);
    }
  }
};

PortalSession::PortalSession() : impl_(std::make_unique<Impl>()) {
  try {
    impl_->connection = sdbus::createSystemBusConnection();
    impl_->proxy = sdbus::createProxy(
        *impl_->connection,
        sdbus::ServiceName{PORTAL_SERVICE},
        sdbus::ObjectPath{OBJECT_PATH});
    // Signals (Request::Response) are only delivered while the connection runs
    // its event loop; do this on a background thread so blocking waits work.
    impl_->connection->enterEventLoopAsync();
  } catch (const std::exception& e) {
    impl_->last_error = "failed to connect to D-Bus: " + std::string(e.what());
  }
}

PortalSession::~PortalSession() {
  close();
}

std::expected<void, std::string> PortalSession::create_session() {
  if (!impl_->proxy) {
    return std::unexpected(impl_->last_error.empty() ? "D-Bus proxy not available" : impl_->last_error);
  }

  impl_->update_status(PortalStatus::CreatingSession);

  try {
    PortalResults options;
    sdbus::ObjectPath request_path;
    impl_->proxy->callMethod("CreateSession")
      .onInterface(SCREENCAST_INTERFACE)
      .withArguments(options)
      .storeResultsTo(request_path);

    PortalResponse response;
    std::string error;
    if (!wait_for_response(*impl_->connection, request_path, response, error, std::chrono::seconds(60))) {
      impl_->update_status(PortalStatus::Failed, "CreateSession failed: " + error);
      return std::unexpected(impl_->last_error);
    }

    const auto it = response.results.find("session_handle");
    if (it == response.results.end()) {
      impl_->update_status(PortalStatus::Failed, "CreateSession succeeded but returned no session_handle");
      return std::unexpected(impl_->last_error);
    }
    impl_->session_handle = it->second.get<std::string>();

    impl_->update_status(PortalStatus::Idle);
    return {};
  } catch (const std::exception& e) {
    impl_->update_status(PortalStatus::Failed, "CreateSession failed: " + std::string(e.what()));
    return std::unexpected(impl_->last_error);
  }
}

std::expected<void, std::string> PortalSession::select_sources(const std::vector<std::string>& source_ids) {
  if (!impl_->proxy) {
    return std::unexpected(impl_->last_error.empty() ? "D-Bus proxy not available" : impl_->last_error);
  }
  if (impl_->session_handle.empty()) {
    return std::unexpected("no portal session; call create_session() first");
  }

  impl_->update_status(PortalStatus::SelectingSources);

  try {
    PortalResults options;
    if (!source_ids.empty()) {
      std::vector<sdbus::Variant> requested;
      requested.reserve(source_ids.size());
      for (const auto& id : source_ids) {
        requested.emplace_back(id);
      }
      options["restore_token"] = sdbus::Variant(source_ids.front());
    }

    sdbus::ObjectPath request_path;
    impl_->proxy->callMethod("SelectSources")
      .onInterface(SCREENCAST_INTERFACE)
      .withArguments(sdbus::ObjectPath{impl_->session_handle}, options)
      .storeResultsTo(request_path);

    PortalResponse response;
    std::string error;
    if (!wait_for_response(*impl_->connection, request_path, response, error, std::chrono::seconds(120))) {
      impl_->update_status(PortalStatus::Failed, "SelectSources failed: " + error);
      return std::unexpected(impl_->last_error);
    }

    impl_->update_status(PortalStatus::Idle);
    return {};
  } catch (const std::exception& e) {
    impl_->update_status(PortalStatus::Failed, "SelectSources failed: " + std::string(e.what()));
    return std::unexpected(impl_->last_error);
  }
}

std::expected<void, std::string> PortalSession::start() {
  if (!impl_->proxy) {
    return std::unexpected(impl_->last_error.empty() ? "D-Bus proxy not available" : impl_->last_error);
  }
  if (impl_->session_handle.empty()) {
    return std::unexpected("no portal session; call create_session() first");
  }

  try {
    PortalResults options;
    sdbus::ObjectPath request_path;
    impl_->proxy->callMethod("Start")
      .onInterface(SCREENCAST_INTERFACE)
      .withArguments(sdbus::ObjectPath{impl_->session_handle}, std::string("hyprshare"), options)
      .storeResultsTo(request_path);

    PortalResponse response;
    std::string error;
    if (!wait_for_response(*impl_->connection, request_path, response, error, std::chrono::seconds(60))) {
      impl_->update_status(PortalStatus::Failed, "Start failed: " + error);
      return std::unexpected(impl_->last_error);
    }

    // Start's response carries "streams": a(ua{sv}) with (pipewire_node_id, props).
    const auto it = response.results.find("streams");
    if (it != response.results.end()) {
      const auto streams = it->second.get<PortalStreams>();
      if (!streams.empty()) {
        impl_->pipewire_node_id = std::get<0>(streams.front());
      }
    }

    if (impl_->pipewire_node_id == 0) {
      impl_->update_status(PortalStatus::Failed, "Start succeeded but no PipeWire node id was returned");
      return std::unexpected(impl_->last_error);
    }

    impl_->update_status(PortalStatus::Started);
    return {};
  } catch (const std::exception& e) {
    impl_->update_status(PortalStatus::Failed, "Start failed: " + std::string(e.what()));
    return std::unexpected(impl_->last_error);
  }
}

std::expected<uint32_t, std::string> PortalSession::get_pipewire_node_id() const {
  if (impl_->pipewire_node_id == 0) {
    return std::unexpected("PipeWire node ID not available");
  }
  return impl_->pipewire_node_id;
}

std::expected<std::vector<ScreenSource>, std::string> PortalSession::list_sources() const {
  return impl_->available_sources;
}

void PortalSession::set_status_callback(StatusCallback callback) {
  impl_->callback = std::move(callback);
}

PortalStatus PortalSession::status() const {
  return impl_->current_status;
}

std::string PortalSession::error() const {
  return impl_->last_error;
}

void PortalSession::close() {
  if (impl_->session_handle.empty() && !impl_->proxy) {
    return;
  }

  if (!impl_->session_handle.empty()) {
    try {
      auto session_proxy = sdbus::createProxy(
          *impl_->connection,
          sdbus::ServiceName{PORTAL_SERVICE},
          sdbus::ObjectPath{impl_->session_handle});
      session_proxy->callMethod("Close").onInterface("org.freedesktop.portal.Session");
    } catch (...) {
    }
    impl_->session_handle.clear();
  }

  if (impl_->connection) {
    try {
      impl_->connection->leaveEventLoop();
    } catch (...) {
    }
  }

  impl_->proxy.reset();
  impl_->connection.reset();
}

}  // namespace hyprshare::portal
