#include "cmd_inbox.hpp"

#include <filesystem>
#include <iostream>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/core/discovery.hpp"
#include "hyprshare/core/inbox.hpp"
#include "hyprshare/core/inbox_history.hpp"
#include "hyprshare/perf/performance.hpp"
#include "hyprshare/util/string.hpp"

namespace hyprshare::cli {

using hyprshare::core::InboxAnnouncePlan;
using hyprshare::core::InboxExtractResult;
using hyprshare::core::InboxUploadResult;
using hyprshare::model::InboxReceivedFile;
using hyprshare::platform::Paths;

namespace {

bool is_inbox_archive_file(const InboxReceivedFile& received_file) {
  const std::string lower_name = hyprshare::util::to_lower(received_file.file_name);
  return lower_name.ends_with(".tar.gz") || lower_name.ends_with(".tgz");
}

std::expected<std::vector<InboxExtractResult>, std::string> extract_received_inbox_archives(
  const std::vector<InboxReceivedFile>& received_files,
  const std::filesystem::path& destination,
  const hyprshare::platform::ProcessRunner& runner) {
  std::vector<InboxExtractResult> extracted_archives;
  for (const auto& received_file : received_files) {
    if (!is_inbox_archive_file(received_file)) {
      continue;
    }

    const auto extracted = hyprshare::core::extract_inbox_archive(received_file.destination_path, destination, runner);
    if (!extracted.has_value()) {
      return std::unexpected("failed to extract received archive `" + received_file.file_name + "`: " + extracted.error());
    }
    extracted_archives.push_back(*extracted);
  }
  return extracted_archives;
}

int run_inbox_receiver(
  CommandContext& ctx,
  const std::string& host,
  const std::uint16_t port,
  const std::filesystem::path& destination,
  const std::uint32_t upload_count,
  const std::uint32_t idle_timeout,
  const std::optional<std::string>& token,
  const std::uintmax_t max_upload_bytes,
  const bool announce,
  const std::string& announce_name,
  const bool extract_archives) {
  if (!ctx.json) {
    print_inbox_serve_hint(host, port, destination, upload_count, idle_timeout, max_upload_bytes, token.value_or(std::string()));
  }

  std::optional<int> announcement_pid;
  if (announce) {
    const auto announce_plan =
      hyprshare::core::plan_inbox_announce(announce_name, port, token.has_value() && !token->empty());
    const auto started = hyprshare::core::start_inbox_announce(announce_plan, ctx.runner);
    if (!started.has_value()) {
      std::cerr << "error: " << started.error() << "\n";
      return 1;
    }
    announcement_pid = started->pid;
    if (!ctx.json) {
      std::cout << "\nAnnouncement active: " << started->plan.name << " "
                << started->plan.service_type << " port=" << started->plan.port
                << " pid=" << started->pid << "\n";
    }
  }

  const auto received =
    hyprshare::core::serve_inbox(
      host, port, destination, upload_count, token.value_or(std::string()), idle_timeout, max_upload_bytes, &ctx.runner, ctx.tls_config);
  if (announcement_pid.has_value()) {
    const auto stopped = ctx.runner.terminate_detached_process_group(*announcement_pid);
    if (!stopped.has_value()) {
      std::cerr << "warning: " << stopped.error() << "\n";
    }
  }
  if (!received.has_value()) {
    std::cerr << "error: " << received.error() << "\n";
    return 1;
  }

  for (const auto& received_file : *received) {
    const auto recorded = hyprshare::core::append_inbox_history(received_file, ctx.paths);
    if (!recorded.has_value()) {
      std::cerr << "error: " << recorded.error() << "\n";
      return 1;
    }
  }

  std::vector<InboxExtractResult> extracted_archives;
  if (extract_archives) {
    const auto extracted = extract_received_inbox_archives(*received, destination, ctx.runner);
    if (!extracted.has_value()) {
      std::cerr << "error: " << extracted.error() << "\n";
      return 1;
    }
    extracted_archives = *extracted;
  }

  if (ctx.json && extract_archives) {
    print_inbox_received_with_extract_json(*received, extracted_archives);
  } else if (received->size() == 1) {
    if (ctx.json) {
      print_inbox_received_json(received->front());
    } else {
      print_inbox_received_text(received->front());
      print_inbox_auto_extract_text(extracted_archives);
    }
  } else if (ctx.json) {
    print_inbox_received_list_json(*received);
  } else if (received->empty()) {
    std::cout << "No inbox uploads received before timeout.\n";
  } else {
    print_inbox_received_list_text(*received);
    print_inbox_auto_extract_text(extracted_archives);
  }
  return 0;
}

}  // namespace

std::expected<int, std::string> handle_inbox(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string("unknown inbox subcommand"));
  }

  if (args[1] == "status") {
    const auto receive_dir = resolve_user_path(ctx.config.files.receive_dir);
    if (!receive_dir.has_value()) {
      std::cerr << "error: " << receive_dir.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      print_inbox_status_json(ctx.config, *receive_dir);
    } else {
      print_inbox_status_text(ctx.config, *receive_dir);
    }
    return 0;
  }

  if (args[1] == "history" && (args.size() == 2 || (args.size() == 3 && args[2] == "clear"))) {
    if (args.size() == 3 && args[2] == "clear") {
      const auto cleared = hyprshare::core::clear_inbox_history(ctx.paths);
      if (!cleared.has_value()) {
        std::cerr << "error: " << cleared.error() << "\n";
        return 1;
      }
      if (ctx.json) {
        print_cleared_inbox_history_json(*cleared);
      } else {
        print_cleared_inbox_history_text(*cleared);
      }
      return 0;
    }

    const auto history = hyprshare::core::list_inbox_history(ctx.paths);
    if (!history.has_value()) {
      std::cerr << "error: " << history.error() << "\n";
      return 1;
    }
    if (ctx.json) {
      print_inbox_history_json(*history);
    } else {
      print_inbox_history_text(*history);
    }
    return 0;
  }

  if (args[1] == "open") {
    const auto receive_dir = resolve_user_path(ctx.config.files.receive_dir);
    if (!receive_dir.has_value()) {
      std::cerr << "error: " << receive_dir.error() << "\n";
      return 1;
    }

    const std::string command = "xdg-open " + hyprshare::util::shell_quote(receive_dir->string());
    if (!ctx.dry_run) {
      std::error_code error;
      std::filesystem::create_directories(*receive_dir, error);
      if (error) {
        std::cerr << "error: failed to create inbox directory `" << *receive_dir << "`: " << error.message() << "\n";
        return 1;
      }
    }
    if (!ctx.dry_run && !ctx.runner.command_exists("xdg-open")) {
      std::cerr << "error: xdg-open is not available; inbox directory is " << *receive_dir << "\n";
      return 1;
    }
    if (!ctx.dry_run) {
      const auto opened = ctx.runner.spawn_detached_shell(command);
      if (!opened.has_value()) {
        std::cerr << "error: " << opened.error() << "\n";
        return 1;
      }
    }

    if (ctx.json) {
      print_inbox_open_json(*receive_dir, command, ctx.dry_run);
    } else {
      print_inbox_open_text(*receive_dir, command, ctx.dry_run);
    }
    return 0;
  }

  if (args[1] == "announce") {
    const auto port_override = take_option(args, "--port");
    const auto name_override = take_option(args, "--name");
    const bool token_required = take_flag(args, "--token-required");

    const auto port = port_override.has_value() ? parse_port_value(*port_override) : std::expected<std::uint16_t, std::string>(28556);
    if (!port.has_value()) {
      std::cerr << "error: " << port.error() << "\n";
      return 1;
    }

    const auto plan = hyprshare::core::plan_inbox_announce(name_override.value_or(std::string()), *port, token_required);
    if (ctx.dry_run) {
      if (ctx.json) {
        print_inbox_announce_json(plan, std::nullopt, true);
      } else {
        print_inbox_announce_text(plan, std::nullopt, true);
      }
      return 0;
    }

    const auto started = hyprshare::core::start_inbox_announce(plan, ctx.runner);
    if (!started.has_value()) {
      std::cerr << "error: " << started.error() << "\n";
      return 1;
    }
    if (ctx.json) {
      print_inbox_announce_json(started->plan, started->pid, false);
    } else {
      print_inbox_announce_text(started->plan, started->pid, false);
    }
    return 0;
  }

  if (args[1] == "receive") {
    const auto port_override = take_option(args, "--port");
    const auto destination_override = take_option(args, "--to");
    const auto count_override = take_option(args, "--count");
    const auto timeout_override = take_option(args, "--timeout");
    const auto max_bytes_override = take_option(args, "--max-bytes");
    const auto announce_name = take_option(args, "--name");
    const bool no_announce = take_flag(args, "--no-announce");
    const bool extract_archives = take_flag(args, "--extract");
    const bool generate_token = take_flag(args, "--generate-token");
    const auto token = take_option(args, "--token");
    const auto receiver_token = resolve_receiver_token(token, generate_token, ctx.json);
    if (!receiver_token.has_value()) {
      std::cerr << "error: " << receiver_token.error() << "\n";
      return 1;
    }

    const auto port = port_override.has_value() ? parse_port_value(*port_override) : std::expected<std::uint16_t, std::string>(28556);
    if (!port.has_value()) {
      std::cerr << "error: " << port.error() << "\n";
      return 1;
    }

    const auto upload_count =
      count_override.has_value() ? parse_count_value(*count_override) : std::expected<std::uint32_t, std::string>(1U);
    if (!upload_count.has_value()) {
      std::cerr << "error: " << upload_count.error() << "\n";
      return 1;
    }

    const auto idle_timeout =
      timeout_override.has_value() ? parse_timeout_value(*timeout_override) : std::expected<std::uint32_t, std::string>(120U);
    if (!idle_timeout.has_value()) {
      std::cerr << "error: " << idle_timeout.error() << "\n";
      return 1;
    }

    const auto max_upload_bytes = max_bytes_override.has_value()
                                    ? parse_byte_count_value(*max_bytes_override)
                                    : std::expected<std::uintmax_t, std::string>(0U);
    if (!max_upload_bytes.has_value()) {
      std::cerr << "error: " << max_upload_bytes.error() << "\n";
      return 1;
    }

    const std::filesystem::path destination = destination_override.has_value()
                                                ? std::filesystem::path(*destination_override)
                                                : std::filesystem::path(ctx.config.files.receive_dir);
    const auto discovery_status = hyprshare::core::inspect_discovery_status(ctx.runner);
    const bool announce = !no_announce && discovery_status.ready_to_announce;
    if (!announce && !no_announce && !ctx.json) {
      std::cout << "Discovery announcement unavailable; receiving without mDNS announcement.\n";
    }

    return run_inbox_receiver(
      ctx,
      "0.0.0.0",
      *port,
      destination,
      *upload_count,
      *idle_timeout,
      *receiver_token,
      *max_upload_bytes,
      announce,
      announce_name.value_or(std::string()),
      extract_archives);
  }

  if (args[1] == "serve") {
    const auto host_override = take_option(args, "--host");
    const auto port_override = take_option(args, "--port");
    const auto destination_override = take_option(args, "--to");
    const auto count_override = take_option(args, "--count");
    const auto timeout_override = take_option(args, "--timeout");
    const auto max_bytes_override = take_option(args, "--max-bytes");
    const auto announce_name = take_option(args, "--announce-name");
    const bool announce = take_flag(args, "--announce");
    const bool extract_archives = take_flag(args, "--extract");
    const bool generate_token = take_flag(args, "--generate-token");
    const auto token = take_option(args, "--token");
    const auto receiver_token = resolve_receiver_token(token, generate_token, ctx.json);
    if (!receiver_token.has_value()) {
      std::cerr << "error: " << receiver_token.error() << "\n";
      return 1;
    }

    const std::string host = host_override.value_or("0.0.0.0");
    const auto port = port_override.has_value() ? parse_port_value(*port_override) : std::expected<std::uint16_t, std::string>(28556);
    if (!port.has_value()) {
      std::cerr << "error: " << port.error() << "\n";
      return 1;
    }

    const auto upload_count =
      count_override.has_value() ? parse_count_value(*count_override) : std::expected<std::uint32_t, std::string>(1U);
    if (!upload_count.has_value()) {
      std::cerr << "error: " << upload_count.error() << "\n";
      return 1;
    }

    const auto idle_timeout = timeout_override.has_value()
                                ? parse_timeout_value(*timeout_override)
                                : std::expected<std::uint32_t, std::string>(0U);
    if (!idle_timeout.has_value()) {
      std::cerr << "error: " << idle_timeout.error() << "\n";
      return 1;
    }

    const auto max_upload_bytes = max_bytes_override.has_value()
                                    ? parse_byte_count_value(*max_bytes_override)
                                    : std::expected<std::uintmax_t, std::string>(0U);
    if (!max_upload_bytes.has_value()) {
      std::cerr << "error: " << max_upload_bytes.error() << "\n";
      return 1;
    }

    const std::filesystem::path destination = destination_override.has_value()
                                                ? std::filesystem::path(*destination_override)
                                                : std::filesystem::path(ctx.config.files.receive_dir);
    return run_inbox_receiver(
      ctx,
      host,
      *port,
      destination,
      *upload_count,
      *idle_timeout,
      *receiver_token,
      *max_upload_bytes,
      announce,
      announce_name.value_or(std::string()),
      extract_archives);
  }

  if (args[1] == "extract") {
    const auto destination_override = take_option(args, "--to");
    if (args.size() != 3) {
      std::cerr << "error: inbox extract requires <archive>\n";
      return 1;
    }

    const std::filesystem::path destination = destination_override.has_value()
                                                ? std::filesystem::path(*destination_override)
                                                : std::filesystem::path(ctx.config.files.receive_dir);
    const auto extracted = hyprshare::core::extract_inbox_archive(args[2], destination, ctx.runner, ctx.dry_run);
    if (!extracted.has_value()) {
      std::cerr << "error: " << extracted.error() << "\n";
      return 1;
    }

    if (ctx.json) {
      print_inbox_extract_json(*extracted);
    } else {
      print_inbox_extract_text(*extracted);
    }
    return 0;
  }

  if (args[1] == "put" && args.size() >= 3) {
    const auto name_override = take_option(args, "--name");
    const auto token = take_option(args, "--token");
    const auto discovered_target = take_option(args, "--to-discovered");

    std::string host;
    std::uint16_t port = 0;
    std::vector<std::filesystem::path> file_paths;

    if (discovered_target.has_value()) {
      if (args.size() < 3) {
        std::cerr << "error: inbox put --to-discovered requires at least one path\n";
        return 1;
      }

      const auto discovered = hyprshare::core::browse_inbox_announcements(ctx.runner);
      if (!discovered.has_value()) {
        std::cerr << "error: " << discovered.error() << "\n";
        return 1;
      }
      const auto selected = hyprshare::core::select_discovered_inbox(*discovered, *discovered_target);
      if (!selected.has_value()) {
        std::cerr << "error: " << selected.error() << "\n";
        return 1;
      }
      if (selected->token_required && !token.has_value()) {
        std::cerr << "error: discovered inbox `" << selected->name << "` requires --token\n";
        return 1;
      }

      host = selected->address;
      port = selected->port;
      for (std::size_t index = 2; index < args.size(); ++index) {
        file_paths.emplace_back(args[index]);
      }
    } else {
      if (args.size() < 4) {
        std::cerr << "error: inbox put requires <host:port> <path...> or --to-discovered <name> <path...>\n";
        return 1;
      }
      const auto endpoint = parse_endpoint_value(args[2]);
      if (!endpoint.has_value()) {
        std::cerr << "error: " << endpoint.error() << "\n";
        return 1;
      }
      host = endpoint->first;
      port = endpoint->second;
      for (std::size_t index = 3; index < args.size(); ++index) {
        file_paths.emplace_back(args[index]);
      }
    }

    if (file_paths.size() > 1 && name_override.has_value()) {
      std::cerr << "error: inbox put --name can only be used with a single upload path\n";
      return 1;
    }

    // The progress of the upload goes to the standard error, so that the
    // result on the standard output stays readable.
    hyprshare::perf::ProgressTracker tracker;
    bool tracker_started = false;
    bool progress_shown = false;
    int last_percent = -1;
    const hyprshare::core::UploadProgressCallback progress =
      [&](const std::uint64_t sent, const std::uint64_t total) {
        if (!tracker_started) {
          tracker.start(total);
          tracker_started = true;
        }
        tracker.update(sent);

        // Small files are not worth a line of output.
        if (total < 1024 * 1024) {
          return;
        }
        const auto info = tracker.current();
        const auto percent = static_cast<int>(info.percentage);
        if (percent == last_percent) {
          return;
        }
        last_percent = percent;
        std::cerr << "\rUploading " << percent << "% ("
                  << info.speed_mbps << " MB/s, " << info.remaining_seconds << "s left)";
        progress_shown = true;
      };

    std::vector<InboxUploadResult> uploads;
    uploads.reserve(file_paths.size());
    for (const auto& file_path : file_paths) {
      const auto sent = hyprshare::core::put_inbox_path(
        host,
        port,
        file_path,
        ctx.runner,
        name_override.value_or(std::string()),
        token.value_or(std::string()),
        ctx.tls_config,
        &progress);
      if (!sent.has_value()) {
        if (progress_shown) {
          std::cerr << "\n";
        }
        std::cerr << "error: failed to upload `" << file_path.string() << "`: " << sent.error() << "\n";
        return 1;
      }
      uploads.push_back(*sent);
    }

    if (progress_shown) {
      std::cerr << "\n";
    }

    if (ctx.json) {
      if (uploads.size() == 1) {
        print_inbox_put_json(host, port, file_paths.front(), uploads.front());
      } else {
        print_inbox_put_batch_json(host, port, file_paths, uploads);
      }
    } else if (uploads.size() == 1) {
      print_inbox_put_text(host, port, file_paths.front(), uploads.front());
    } else {
      print_inbox_put_batch_text(host, port, file_paths, uploads);
    }
    return 0;
  }

  return std::unexpected(std::string("unknown inbox subcommand: " + args[1]));
}

}  // namespace hyprshare::cli
