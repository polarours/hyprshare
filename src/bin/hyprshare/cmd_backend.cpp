#include "cmd_backend.hpp"

#include <iostream>

#include "output_json.hpp"
#include "output_text.hpp"
#include "hyprshare/diagnostics/doctor.hpp"

namespace hyprshare::cli {

using hyprshare::model::DoctorReport;

std::expected<int, std::string> handle_backend(CommandContext& ctx, std::vector<std::string>& args) {
  if (args.size() < 2 || args[1] != "list") {
    return std::unexpected(std::string("unknown backend subcommand"));
  }

  const DoctorReport report = ctx.doctor.run(ctx.paths, ctx.adapters);
  if (ctx.json) {
    print_backends_json(ctx.backends, report);
  } else {
    print_backends_text(ctx.backends, report);
  }
  return 0;
}

}  // namespace hyprshare::cli
