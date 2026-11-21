#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <thread>

#include "hyprshare/cloud/cloud_sync.hpp"

#include "test_support.hpp"

namespace {

using test_support::expect;

// A tiny object store: it keeps whatever is PUT to it and serves it back.
class ObjectStore {
 public:
  ObjectStore() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
      return;
    }
    int reuse = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listen_fd_, 8) != 0) {
      close(listen_fd_);
      listen_fd_ = -1;
      return;
    }

    sockaddr_in bound{};
    socklen_t length = sizeof(bound);
    getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&bound), &length);
    port_ = ntohs(bound.sin_port);
    thread_ = std::thread([this]() { serve(); });
  }

  ~ObjectStore() { stop(); }

  bool valid() const { return listen_fd_ >= 0 && port_ != 0; }
  std::uint16_t port() const { return port_; }
  std::string endpoint() const { return "http://127.0.0.1:" + std::to_string(port_); }

  void stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
      ::shutdown(listen_fd_, SHUT_RDWR);
    }
    if (thread_.joinable()) {
      thread_.join();
    }
    if (listen_fd_ >= 0) {
      close(listen_fd_);
      listen_fd_ = -1;
    }
  }

  std::string last_authorization() const { return last_authorization_; }
  std::size_t object_count() const { return objects_.size(); }

 private:
  void serve() {
    while (running_) {
      const int client = accept(listen_fd_, nullptr, nullptr);
      if (client < 0) {
        break;
      }
      handle(client);
      close(client);
    }
  }

  void handle(const int client) {
    std::string request;
    char buffer[8192];
    while (request.find("\r\n\r\n") == std::string::npos) {
      const ssize_t received = recv(client, buffer, sizeof(buffer), 0);
      if (received <= 0) {
        return;
      }
      request.append(buffer, static_cast<std::size_t>(received));
    }

    const auto header_end = request.find("\r\n\r\n");
    const std::string head = request.substr(0, header_end);
    std::string body = request.substr(header_end + 4);

    const auto length_pos = head.find("Content-Length:");
    if (length_pos != std::string::npos) {
      const std::size_t expected = std::stoul(head.substr(length_pos + 15));
      while (body.size() < expected) {
        const ssize_t received = recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0) {
          break;
        }
        body.append(buffer, static_cast<std::size_t>(received));
      }
    }

    const auto auth_pos = head.find("Authorization:");
    if (auth_pos != std::string::npos) {
      const auto end = head.find("\r\n", auth_pos);
      last_authorization_ = head.substr(auth_pos, end - auth_pos);
    } else {
      last_authorization_.clear();
    }

    // The request line: METHOD PATH HTTP/1.1
    const auto method_end = head.find(' ');
    const auto path_start = method_end + 1;
    const auto path_end = head.find(' ', path_start);
    const std::string method = head.substr(0, method_end);
    const std::string path = head.substr(path_start, path_end - path_start);

    std::string response;
    if (method == "PUT") {
      objects_[path] = body;
      response = "HTTP/1.1 201 Created\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    } else if (method == "GET") {
      const auto found = objects_.find(path);
      if (found == objects_.end()) {
        response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      } else {
        response = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(found->second.size()) +
                   "\r\nConnection: close\r\n\r\n" + found->second;
      }
    } else if (method == "HEAD") {
      const auto found = objects_.find(path);
      if (found == objects_.end()) {
        response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      } else {
        response = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(found->second.size()) +
                   "\r\nConnection: close\r\n\r\n";
      }
    } else {
      response = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    }

    send(client, response.data(), response.size(), MSG_NOSIGNAL);
  }

 private:
  int listen_fd_ = -1;
  std::uint16_t port_ = 0;
  std::atomic<bool> running_{true};
  std::thread thread_;
  std::map<std::string, std::string> objects_;
  std::string last_authorization_;
};

}  // namespace

int main() {
  const auto temp_root = std::filesystem::temp_directory_path() / "hyprshare_cloud_tests";
  std::filesystem::remove_all(temp_root);
  std::filesystem::create_directories(temp_root);

  // The client resolves the state directory from $HOME, so the test points it
  // at a throwaway home.
  const char* original_home = getenv("HOME");
  const std::string saved_home = original_home != nullptr ? original_home : "";
  setenv("HOME", temp_root.c_str(), 1);

  ObjectStore store;
  expect(store.valid(), "the object store should start");

  if (store.valid()) {
    hyprshare::cloud::CloudConfig config;
    config.provider = "remote";
    config.type = hyprshare::cloud::kHttpProvider;
    config.endpoint = store.endpoint();
    config.bucket = "files";
    config.access_key = "token-123";
    config.enabled = true;

    hyprshare::cloud::CloudSyncClient client;
    const auto configured = client.configure(config);
    expect(configured.has_value(), "the http provider should be configurable");
    expect(client.object_url("a/b.txt") == store.endpoint() + "/files/a/b.txt",
      "the object URL should combine endpoint, bucket, and key");

    const auto source = temp_root / "upload.txt";
    {
      std::ofstream out(source);
      out << "cloud payload";
    }

    std::vector<hyprshare::cloud::SyncStatus> observed_statuses;
    client.set_sync_callback([&](const hyprshare::cloud::SyncProgress& progress) {
      observed_statuses.push_back(progress.status);
    });

    const auto uploaded = client.upload(source, "docs/upload.txt");
    expect(uploaded.has_value(),
      "the upload should succeed: " + (uploaded.has_value() ? "" : uploaded.error()));
    expect(std::find(observed_statuses.begin(), observed_statuses.end(),
             hyprshare::cloud::SyncStatus::Uploading) != observed_statuses.end(),
      "an upload should report the Uploading status through the callback");
    expect(client.status() == hyprshare::cloud::SyncStatus::Idle,
      "the client should return to Idle after a successful upload");
    expect(store.object_count() == 1, "the server should have received the object");
    expect(store.last_authorization().find("Bearer token-123") != std::string::npos,
      "the access key should be sent as a bearer token");

    const auto downloaded = client.download("docs/upload.txt", temp_root / "downloaded.txt");
    expect(downloaded.has_value(),
      "the download should succeed: " + (downloaded.has_value() ? "" : downloaded.error()));
    {
      std::ifstream in(temp_root / "downloaded.txt");
      std::string content;
      std::getline(in, content);
      expect(content == "cloud payload", "the downloaded content should match");
    }

    const auto listed = client.list_files("docs");
    expect(listed.has_value(), "listing should succeed");
    if (listed.has_value()) {
      expect(listed->size() == 1, "the uploaded object should be listed");
      if (!listed->empty()) {
        expect(listed->front() == "docs/upload.txt", "the listed key should be the remote path");
      }
    }

    const auto missing = client.download("docs/absent.txt", temp_root / "absent.txt");
    expect(!missing.has_value(), "downloading a missing object should fail");
  }

  if (original_home != nullptr) {
    setenv("HOME", saved_home.c_str(), 1);
  } else {
    unsetenv("HOME");
  }
  std::filesystem::remove_all(temp_root);

  return test_support::report("cloud");
}
