#include "hyprshare/platform/net.hpp"
#include "hyprshare/web/api_server.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

namespace hyprshare::web {

namespace {

// The largest request body the server accepts.
constexpr std::size_t kMaxBodyBytes = 64u * 1024u * 1024u;


const char* reason_phrase(const int status) {
  switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 500: return "Internal Server Error";
    default: return "OK";
  }
}

const char* content_type_for(const std::filesystem::path& path) {
  const auto extension = path.extension().string();
  if (extension == ".html") return "text/html; charset=utf-8";
  if (extension == ".js") return "text/javascript; charset=utf-8";
  if (extension == ".css") return "text/css; charset=utf-8";
  if (extension == ".json" || extension == ".map") return "application/json";
  if (extension == ".svg") return "image/svg+xml";
  if (extension == ".png") return "image/png";
  if (extension == ".ico") return "image/x-icon";
  if (extension == ".woff2") return "font/woff2";
  return "application/octet-stream";
}

// Resolve a request path against the static directory. Reject anything that
// escapes the directory via ".." so the web UI cannot expose arbitrary files.
std::filesystem::path resolve_static(const std::filesystem::path& root, const std::string& url_path) {
  std::filesystem::path relative(url_path);
  if (relative.is_absolute() || !relative.has_root_directory()) {
    // A URL path always starts with '/'; strip it before joining.
    relative = std::filesystem::path(url_path).relative_path();
  }
  const auto candidate = (root / relative).lexically_normal();
  const auto root_normal = root.lexically_normal();
  const auto root_string = root_normal.string();
  const auto candidate_string = candidate.string();
  if (candidate_string != root_string &&
      candidate_string.rfind(root_string + std::filesystem::path::preferred_separator, 0) != 0) {
    return {};
  }
  return candidate;
}

ApiResponse serve_file(const std::filesystem::path& file) {
  std::ifstream input(file, std::ios::binary);
  if (!input.is_open()) {
    return {};
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  ApiResponse response;
  response.status = 200;
  response.content_type = content_type_for(file);
  response.body = buffer.str();
  return response;
}

}  // namespace

ApiServer::ApiServer(const uint16_t port, std::string bind_host)
  : port_(port), bind_host_(std::move(bind_host)) {}

std::string ApiServer::bind_host() const {
  return bind_host_;
}

uint16_t ApiServer::port() const {
  return port_;
}

ApiServer::~ApiServer() {
  stop();
}

std::expected<void, std::string> ApiServer::start() {
  server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd_ < 0) {
    return std::unexpected("failed to create socket");
  }

  int reuse = 1;
  setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);

  if (bind_host_ == "0.0.0.0") {
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
  } else if (inet_pton(AF_INET, bind_host_.c_str(), &addr.sin_addr) != 1) {
    close(server_fd_);
    return std::unexpected("the bind address is not an IPv4 address: " + bind_host_);
  }

  if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    close(server_fd_);
    return std::unexpected("failed to bind to " + bind_host_ + ":" + std::to_string(port_));
  }

  if (listen(server_fd_, 5) != 0) {
    close(server_fd_);
    return std::unexpected("failed to listen");
  }

  running_ = true;
  accept_thread_ = std::thread(&ApiServer::accept_loop, this);

  std::cout << "API server listening on " << bind_host_ << ":" << port_ << std::endl;
  return {};
}

void ApiServer::stop() {
  running_ = false;
  if (server_fd_ >= 0) {
    // shutdown() wakes up the thread that is waiting in accept().
    ::shutdown(server_fd_, SHUT_RDWR);
  }
  // The thread has to be finished before the descriptor is closed, otherwise
  // the number can be handed to another socket while the thread still waits
  // on it.
  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }
  if (server_fd_ >= 0) {
    close(server_fd_);
    server_fd_ = -1;
  }
}

bool ApiServer::is_running() const {
  return running_;
}

void ApiServer::register_handler(const std::string& method, const std::string& path, ApiHandler handler) {
  handlers_.push_back({method + " " + path, handler});
}

void ApiServer::set_static_dir(std::filesystem::path dir) {
  static_dir_ = std::move(dir);
}

void ApiServer::accept_loop() {
  while (running_) {
    const auto client_fd = accept(server_fd_, nullptr, nullptr);
    if (client_fd < 0) {
      if (running_) {
        std::cerr << "api: accept failed\n";
      }
      continue;
    }

    std::thread(&ApiServer::handle_client, this, client_fd).detach();
  }
}

void ApiServer::handle_client(const int client_fd) {
  char buffer[8192];
  std::string accumulated;

  while (running_) {
    const auto received = platform::recv_retry(client_fd, buffer, sizeof(buffer) - 1);
    if (received <= 0) {
      break;
    }
    buffer[received] = '\0';
    accumulated += buffer;

    if (accumulated.find("\r\n\r\n") != std::string::npos) {
      break;
    }
  }

  if (accumulated.empty()) {
    close(client_fd);
    return;
  }

  ApiRequest request;
  // The bytes that arrived together with the headers are the beginning of
  // the body. Reading only the headers and then waiting for the whole body
  // would wait for bytes that were already received.
  const auto header_end = accumulated.find("\r\n\r\n");
  request.body = header_end == std::string::npos ? "" : accumulated.substr(header_end + 4);

  std::istringstream stream(accumulated);
  std::string line;
  std::getline(stream, line);
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }

  std::istringstream request_line(line);
  request_line >> request.method >> request.path;

  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      break;
    }
    const auto colon = line.find(':');
    if (colon != std::string::npos) {
      request.headers.push_back({line.substr(0, colon), line.substr(colon + 2)});
    }
  }

  const auto content_length_it = std::find_if(request.headers.begin(), request.headers.end(),
    [](const auto& h) { return h.first == "Content-Length"; });
  if (content_length_it != request.headers.end()) {
    const auto length = std::stoul(content_length_it->second);
    if (length > kMaxBodyBytes) {
      const std::string response = "{\"error\":\"the request body is too large\"}";
      const std::string message =
        "HTTP/1.1 413 Payload Too Large\r\nContent-Type: application/json\r\n"
        "Content-Length: " + std::to_string(response.size()) + "\r\nConnection: close\r\n\r\n" +
        response;
      send(client_fd, message.data(), message.size(), 0);
      close(client_fd);
      return;
    }

    while (request.body.size() < length) {
      const auto received = platform::recv_retry(client_fd, buffer, sizeof(buffer) - 1);
      if (received <= 0) break;
      request.body.append(buffer, static_cast<std::size_t>(received));
    }
  }

  const auto response = route_request(request);

  std::ostringstream http_response;
  http_response << "HTTP/1.1 " << response.status << " " << reason_phrase(response.status) << "\r\n";
  http_response << "Content-Type: " << response.content_type << "\r\n";
  http_response << "Content-Length: " << response.body.size() << "\r\n";
  http_response << "Access-Control-Allow-Origin: *\r\n";
  http_response << "Connection: close\r\n\r\n";
  http_response << response.body;

  const auto response_str = http_response.str();
  send(client_fd, response_str.c_str(), response_str.size(), 0);
  close(client_fd);
}

ApiResponse ApiServer::route_request(const ApiRequest& request) {
  std::string path = request.path;
  const auto query_start = path.find('?');
  if (query_start != std::string::npos) {
    path = path.substr(0, query_start);
  }
  const auto key = request.method + " " + path;
  for (const auto& [pattern, handler] : handlers_) {
    if (key == pattern) {
      return handler(request);
    }
  }

  // No API route matched. Serve the web frontend (if configured) so a single
  // `hyprshare web serve` provides both the API and the UI.
  if (request.method == "GET" && !static_dir_.empty()) {
    const auto resolved = resolve_static(static_dir_, path);
    if (!resolved.empty() && std::filesystem::is_regular_file(resolved)) {
      auto response = serve_file(resolved);
      if (response.status == 200) {
        return response;
      }
    }
    // Client-side routes (e.g. /transfer) have no file: hand out index.html.
    // Only extension-less paths are SPA routes — keep 404 for unknown API
    // paths and missing assets so mistakes stay visible.
    if (std::filesystem::path(path).extension().empty() && path.rfind("/api/", 0) != 0) {
      auto index = serve_file(static_dir_ / "index.html");
      if (index.status == 200) {
        return index;
      }
    }
  }

  ApiResponse response;
  response.status = 404;
  response.body = "{\"error\":\"not found\"}";
  return response;
}

}  // namespace hyprshare::web
