#include "hyprshare/daemon/ipc.hpp"

#include <cstring>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace hyprshare::daemon {

namespace {

std::string escape_json_string(const std::string_view value) {
  std::string result;
  result.reserve(value.size() + 2);
  result.push_back('"');
  for (const auto ch : value) {
    switch (ch) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default: result.push_back(ch); break;
    }
  }
  result.push_back('"');
  return result;
}

}  // namespace

std::string serialize_response(const IpcResponse& response) {
  std::ostringstream stream;
  stream << "{"
         << "\"ok\":" << (response.ok ? "true" : "false") << ","
         << "\"error\":" << escape_json_string(response.error) << ","
         << "\"result\":{";

  bool first = true;
  for (const auto& [key, value] : response.result) {
    if (!first) {
      stream << ",";
    }
    stream << escape_json_string(key) << ":" << escape_json_string(value);
    first = false;
  }

  stream << "}}\n";
  return stream.str();
}

std::string serialize_request(const IpcRequest& request) {
  std::ostringstream stream;
  stream << "{"
         << "\"command\":" << escape_json_string(request.command) << ","
         << "\"args\":{";

  bool first = true;
  for (const auto& [key, value] : request.args) {
    if (!first) {
      stream << ",";
    }
    stream << escape_json_string(key) << ":" << escape_json_string(value);
    first = false;
  }

  stream << "}}\n";
  return stream.str();
}

// The position of the brace that closes the object that starts at the given
// position.
std::size_t find_object_end(const std::string& json, const std::size_t object_start) {
  int depth = 0;
  bool in_string = false;
  bool escaped = false;

  for (std::size_t index = object_start; index < json.size(); ++index) {
    const char character = json[index];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (character == '\\') {
      escaped = true;
      continue;
    }
    if (character == '"') {
      in_string = !in_string;
      continue;
    }
    if (in_string) {
      continue;
    }
    if (character == '{') {
      ++depth;
    } else if (character == '}') {
      --depth;
      if (depth == 0) {
        return index;
      }
    }
  }

  return std::string::npos;
}

// Reads a quoted JSON string, and returns the position after it.
std::optional<std::pair<std::string, std::size_t>> read_json_string(
  const std::string& json,
  const std::size_t from) {
  const auto start = json.find('"', from);
  if (start == std::string::npos) {
    return std::nullopt;
  }

  std::string value;
  for (std::size_t index = start + 1; index < json.size(); ++index) {
    const char character = json[index];
    if (character == '"') {
      return std::make_pair(value, index + 1);
    }
    if (character == '\\' && index + 1 < json.size()) {
      const char escaped = json[index + 1];
      switch (escaped) {
        case 'n': value += '\n'; break;
        case 'r': value += '\r'; break;
        case 't': value += '\t'; break;
        default: value += escaped; break;
      }
      ++index;
      continue;
    }
    value += character;
  }
  return std::nullopt;
}

// Splits the content of a JSON object on the commas that are not inside a
// string.
std::vector<std::string> split_object_entries(const std::string& content) {
  std::vector<std::string> entries;
  std::string current;
  bool in_string = false;
  bool escaped = false;

  for (const char character : content) {
    if (escaped) {
      current += character;
      escaped = false;
      continue;
    }
    if (character == '\\') {
      current += character;
      escaped = true;
      continue;
    }
    if (character == '"') {
      in_string = !in_string;
      current += character;
      continue;
    }
    if (character == ',' && !in_string) {
      entries.push_back(current);
      current.clear();
      continue;
    }
    current += character;
  }

  if (!current.empty()) {
    entries.push_back(current);
  }
  return entries;
}

std::expected<IpcResponse, std::string> parse_response(const std::string& json) {
  IpcResponse response;

  const auto ok_pos = json.find("\"ok\":");
  if (ok_pos == std::string::npos) {
    return std::unexpected("missing 'ok' field");
  }
  const auto ok_value_start = ok_pos + 5;
  while (ok_value_start < json.size() && json[ok_value_start] == ' ') {
    // skip whitespace
  }
  if (json.substr(ok_value_start, 4) == "true") {
    response.ok = true;
  } else if (json.substr(ok_value_start, 5) == "false") {
    response.ok = false;
  } else {
    return std::unexpected("invalid 'ok' value");
  }

  const auto error_pos = json.find("\"error\":");
  if (error_pos != std::string::npos) {
    const auto error_value = read_json_string(json, error_pos + 8);
    if (error_value.has_value()) {
      response.error = error_value->first;
    }
  }

  // The result is a flat object of strings. It used to be skipped, which left
  // the caller without any answer.
  const auto result_pos = json.find("\"result\":");
  if (result_pos == std::string::npos) {
    return response;
  }

  const auto object_start = json.find('{', result_pos);
  if (object_start == std::string::npos) {
    return response;
  }

  const auto object_end = find_object_end(json, object_start);
  if (object_end == std::string::npos) {
    return response;
  }

  const std::string content = json.substr(object_start + 1, object_end - object_start - 1);
  for (const auto& entry : split_object_entries(content)) {
    const auto colon = entry.find(':');
    if (colon == std::string::npos) {
      continue;
    }

    const auto key = read_json_string(entry, 0);
    if (!key.has_value()) {
      continue;
    }
    const auto value = read_json_string(entry, colon + 1);
    if (!value.has_value()) {
      continue;
    }
    response.result[key->first] = value->first;
  }

  return response;
}


std::expected<IpcRequest, std::string> parse_request(const std::string& json) {
  IpcRequest request;

  const auto cmd_pos = json.find("\"command\":");
  if (cmd_pos == std::string::npos) {
    return std::unexpected("missing 'command' field");
  }
  const auto cmd_value_start = json.find('"', cmd_pos + 10) + 1;
  const auto cmd_value_end = json.find('"', cmd_value_start);
  if (cmd_value_end == std::string::npos) {
    return std::unexpected("unclosed 'command' value");
  }
  request.command = json.substr(cmd_value_start, cmd_value_end - cmd_value_start);

  // The arguments are a flat object of strings. They used to be dropped, which
  // left every command with its defaults.
  const auto args_pos = json.find("\"args\":");
  if (args_pos == std::string::npos) {
    return request;
  }

  const auto object_start = json.find('{', args_pos);
  if (object_start == std::string::npos) {
    return request;
  }

  const auto object_end = find_object_end(json, object_start);
  if (object_end == std::string::npos) {
    return request;
  }

  const std::string content = json.substr(object_start + 1, object_end - object_start - 1);
  for (const auto& entry : split_object_entries(content)) {
    const auto colon = entry.find(':');
    if (colon == std::string::npos) {
      continue;
    }

    const auto key = read_json_string(entry, 0);
    if (!key.has_value()) {
      continue;
    }
    const auto value = read_json_string(entry, colon + 1);
    if (!value.has_value()) {
      continue;
    }
    request.args[key->first] = value->first;
  }

  return request;
}


std::filesystem::path daemon_socket_path(const platform::Paths& paths) {
  // The runtime directory already carries the name of the application.
  return paths.runtime_dir / "hyprshared.sock";
}

std::expected<IpcResponse, std::string> send_request(
  const std::filesystem::path& socket_path,
  const IpcRequest& request,
  const std::chrono::milliseconds timeout) {
  std::error_code error;
  if (!std::filesystem::exists(socket_path, error)) {
    return std::unexpected("the daemon is not running, there is no socket at " + socket_path.string());
  }

  const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return std::unexpected("failed to create the socket: " + std::string(std::strerror(errno)));
  }

  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  const std::string path_text = socket_path.string();
  if (path_text.size() >= sizeof(address.sun_path)) {
    close(fd);
    return std::unexpected("the socket path is too long: " + path_text);
  }
  std::memcpy(address.sun_path, path_text.c_str(), path_text.size());

  timeval interval{};
  interval.tv_sec = static_cast<time_t>(timeout.count() / 1000);
  interval.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &interval, sizeof(interval));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &interval, sizeof(interval));

  if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    close(fd);
    return std::unexpected("failed to reach the daemon: " + std::string(std::strerror(errno)));
  }

  const std::string payload = serialize_request(request) + "\n";
  std::size_t offset = 0;
  while (offset < payload.size()) {
    const ssize_t sent = send(fd, payload.data() + offset, payload.size() - offset, MSG_NOSIGNAL);
    if (sent <= 0) {
      close(fd);
      return std::unexpected("failed to send the request: " + std::string(std::strerror(errno)));
    }
    offset += static_cast<std::size_t>(sent);
  }

  std::string response_text;
  char buffer[4096];
  while (response_text.find('\n') == std::string::npos) {
    const ssize_t received = recv(fd, buffer, sizeof(buffer), 0);
    if (received <= 0) {
      break;
    }
    response_text.append(buffer, static_cast<std::size_t>(received));
  }
  close(fd);

  if (response_text.empty()) {
    return std::unexpected("the daemon did not answer");
  }

  return parse_response(response_text);
}

}  // namespace hyprshare::daemon
