#pragma once

#include <atomic>
#include <filesystem>
#include <thread>
#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <vector>

namespace hyprshare::web {

struct ApiRequest {
  std::string method;
  std::string path;
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;
};

struct ApiResponse {
  int status = 200;
  std::string content_type = "application/json";
  std::string body;
};

using ApiHandler = std::function<ApiResponse(const ApiRequest&)>;

class ApiServer {
 public:
  // The server binds to the loopback address by default: the API exposes the
  // local configuration and the staged sessions, so it has to be reachable
  // from the network only when that is asked for.
  explicit ApiServer(uint16_t port = 8080, std::string bind_host = "127.0.0.1");
  ~ApiServer();

  std::string bind_host() const;
  uint16_t port() const;

  std::expected<void, std::string> start();
  void stop();

  void register_handler(const std::string& method, const std::string& path, ApiHandler handler);

  // Serve the web frontend from this directory for GET requests that do not
  // match an API route. Unknown paths fall back to index.html so the SPA can
  // handle its own routes. An empty directory disables static serving.
  void set_static_dir(std::filesystem::path dir);

  bool is_running() const;

 private:
  void accept_loop();
  void handle_client(int client_fd);
  ApiResponse route_request(const ApiRequest& request);

  uint16_t port_;
  std::string bind_host_;
  int server_fd_ = -1;
  std::thread accept_thread_;
  std::atomic<bool> running_{false};
  std::vector<std::pair<std::string, ApiHandler>> handlers_;
  std::filesystem::path static_dir_;
};

}  // namespace hyprshare::web
