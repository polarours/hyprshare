#include "cmd_share.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/core/clipboard_share.hpp"
#include "hyprshare/core/file_share.hpp"

#ifdef HYPRSHARE_HAS_PORTAL
#include "hyprshare/portal/portal_session.hpp"
#include "hyprshare/portal/pipewire_stream.hpp"
#endif

namespace hyprshare::cli {

using hyprshare::model::AppLaunchPlan;
using hyprshare::model::DoctorReport;
using hyprshare::model::FileShareManifest;
using hyprshare::model::SharePlan;

std::expected<int, std::string> handle_share(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2) {
    return std::unexpected(std::string("unknown share subcommand"));
  }

  if (args[1] == "screen") {
    const auto app = take_option(args, "--app");
    const DoctorReport report = ctx.doctor.run(ctx.paths, ctx.adapters, app.has_value() ? std::optional<std::string_view>(*app) : std::nullopt);

    if (app.has_value()) {
      const auto profile = ctx.adapters.find_by_hint(*app);
      if (!profile.has_value()) {
        std::cerr << "error: no compatibility profile matched `" << *app << "`\n";
        return 1;
      }

      const auto available_commands = ctx.launcher.resolve_available_commands(*profile);
      const AppLaunchPlan plan = ctx.planner.plan_app_launch(*app, report.facts, ctx.config, ctx.backends, ctx.adapters, available_commands);
      if (ctx.json) {
        print_launch_plan_json(plan, ctx.dry_run);
      } else {
        print_launch_plan_text(plan, ctx.dry_run);
      }

      if (ctx.dry_run) {
        return (!plan.selected_backend_id.empty() && plan.ready) ? 0 : 1;
      }

      if (!plan.ready || plan.selected_backend_id.empty()) {
        std::cerr << "error: no launchable screen-sharing backend is currently available for `" << *app << "`\n";
        return 1;
      }

      const auto execution = ctx.launcher.execute(plan);
      if (!execution.has_value()) {
        std::cerr << "error: " << execution.error() << "\n";
        return 1;
      }
      return 0;
    }

    const SharePlan plan = ctx.planner.plan_screen("", report.facts, ctx.config, ctx.backends, ctx.adapters);
    if (ctx.json) {
      print_plan_json(plan, ctx.dry_run);
    } else {
      print_plan_text(plan, ctx.dry_run);
    }

    if (ctx.dry_run) {
      return plan.selected_backend_id.empty() ? 1 : 0;
    }

#ifdef HYPRSHARE_HAS_PORTAL
    if (plan.selected_backend_id == "portal-screencast") {
      std::cout << "Starting portal screen capture..." << std::endl;

      hyprshare::portal::PortalSession session;
      session.set_status_callback([](hyprshare::portal::PortalStatus status, const std::string& error) {
        switch (status) {
          case hyprshare::portal::PortalStatus::CreatingSession:
            std::cout << "Creating portal session...\n";
            break;
          case hyprshare::portal::PortalStatus::SelectingSources:
            std::cout << "Selecting screen sources...\n";
            break;
          case hyprshare::portal::PortalStatus::Started:
            std::cout << "Portal session started." << std::endl;
            break;
          case hyprshare::portal::PortalStatus::Failed:
            std::cerr << "Portal error: " << error << "\n";
            break;
          default:
            break;
        }
      });

      auto create_result = session.create_session();
      if (!create_result.has_value()) {
        std::cerr << "error: " << create_result.error() << "\n";
        return 1;
      }

      auto select_result = session.select_sources({});
      if (!select_result.has_value()) {
        std::cerr << "error: " << select_result.error() << "\n";
        return 1;
      }

      auto start_result = session.start();
      if (!start_result.has_value()) {
        std::cerr << "error: " << start_result.error() << "\n";
        return 1;
      }

      auto node_id = session.get_pipewire_node_id();
      if (!node_id.has_value()) {
        std::cerr << "error: " << node_id.error() << "\n";
        session.close();
        return 1;
      }
      std::cout << "PipeWire node ID: " << *node_id << "\n";

      hyprshare::portal::PipeWireStream stream;
      auto frames = std::make_shared<std::atomic<std::uint64_t>>(0);
      stream.set_frame_callback([frames](const void* /*data*/, std::uint32_t /*size*/,
                                         const hyprshare::portal::FrameInfo& /*info*/) {
        frames->fetch_add(1, std::memory_order_relaxed);
      });

      auto stream_connect = stream.connect(*node_id);
      if (!stream_connect.has_value()) {
        std::cerr << "error: " << stream_connect.error() << "\n";
        session.close();
        return 1;
      }

      auto stream_start = stream.start();
      if (!stream_start.has_value()) {
        std::cerr << "error: " << stream_start.error() << "\n";
        stream.disconnect();
        session.close();
        return 1;
      }

      std::cout << "Capturing frames. Press Ctrl+C to stop." << std::endl;
      std::uint64_t last_frames = 0;
      while (session.status() == hyprshare::portal::PortalStatus::Started) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const auto captured = frames->load(std::memory_order_relaxed);
        if (captured != last_frames) {
          std::cout << "frames captured: " << captured << "\n";
          last_frames = captured;
        }
      }

      static_cast<void>(stream.stop());
      stream.disconnect();
      session.close();
      return 0;
    }
#endif

    return plan.selected_backend_id.empty() ? 1 : 0;
  }

  if (args[1] == "clipboard") {
    const auto clipboard = hyprshare::core::read_clipboard_text(ctx.paths, ctx.runner);
    if (!clipboard.has_value()) {
      std::cerr << "error: " << clipboard.error() << "\n";
      return 1;
    }

    if (ctx.stage) {
      const auto staged = hyprshare::core::stage_clipboard_text(*clipboard, ctx.paths, ctx.runner);
      if (!staged.has_value()) {
        std::cerr << "error: " << staged.error() << "\n";
        return 1;
      }

      if (ctx.json) {
        print_staged_share_json(*staged);
      } else {
        print_staged_share_text(*staged);
      }
      return 0;
    }

    if (ctx.json) {
      print_clipboard_text_json(*clipboard);
    } else {
      print_clipboard_summary_text(*clipboard);
    }
    return 0;
  }

  if (args[1] == "file" && args.size() >= 3) {
    const auto manifest = hyprshare::core::build_file_manifest(args[2]);
    if (!manifest.has_value()) {
      std::cerr << "error: " << manifest.error() << "\n";
      return 1;
    }

    if (ctx.stage) {
      const auto staged = hyprshare::core::stage_file_share(*manifest, ctx.paths, ctx.runner);
      if (!staged.has_value()) {
        std::cerr << "error: " << staged.error() << "\n";
        return 1;
      }

      if (ctx.json) {
        print_staged_share_json(*staged);
      } else {
        print_staged_share_text(*staged);
      }
      return 0;
    }

    if (ctx.json) {
      print_file_manifest_json(*manifest);
    } else {
      print_file_manifest_text(*manifest);
    }
    return 0;
  }

  if (args[1] == "folder" && args.size() >= 3) {
    const auto manifest = hyprshare::core::build_folder_manifest(args[2], ctx.config.files);
    if (!manifest.has_value()) {
      std::cerr << "error: " << manifest.error() << "\n";
      return 1;
    }

    if (ctx.stage) {
      const auto staged = hyprshare::core::stage_file_share(*manifest, ctx.paths, ctx.runner);
      if (!staged.has_value()) {
        std::cerr << "error: " << staged.error() << "\n";
        return 1;
      }

      if (ctx.json) {
        print_staged_share_json(*staged);
      } else {
        print_staged_share_text(*staged);
      }
      return 0;
    }

    if (ctx.json) {
      print_file_manifest_json(*manifest);
    } else {
      print_file_manifest_text(*manifest);
    }
    return 0;
  }

  return std::unexpected(std::string("unknown share subcommand: " + args[1]));
}

}  // namespace hyprshare::cli
