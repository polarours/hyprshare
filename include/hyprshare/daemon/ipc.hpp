#pragma once

#include <chrono>
#include <expected>
#include <filesystem>
#include <string>
#include <unordered_map>

#include "hyprshare/platform/paths.hpp"

namespace hyprshare::daemon {

struct IpcRequest {
  std::string command;
  std::unordered_map<std::string, std::string> args;
};

struct IpcResponse {
  bool ok = false;
  std::string error;
  std::unordered_map<std::string, std::string> result;
};

std::string serialize_response(const IpcResponse& response);
std::expected<IpcResponse, std::string> parse_response(const std::string& json);
std::string serialize_request(const IpcRequest& request);
std::expected<IpcRequest, std::string> parse_request(const std::string& json);

// The socket the daemon listens on.
std::filesystem::path daemon_socket_path(const platform::Paths& paths);

// Sends a request to a running daemon and waits for its answer.
std::expected<IpcResponse, std::string> send_request(
  const std::filesystem::path& socket_path,
  const IpcRequest& request,
  std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

}  // namespace hyprshare::daemon
