#include "cmd_status.hpp"

#include <iostream>

#include "command_utils.hpp"
#include "output_json.hpp"
#include "output_text.hpp"

namespace hyprshare::cli {

using hyprshare::model::DoctorReport;

std::expected<int, std::string> handle_status(CommandContext& ctx, [[maybe_unused]] std::vector<std::string>& args) {
  const DoctorReport report = ctx.doctor.run(ctx.paths, ctx.adapters);
  if (ctx.json) {
    print_status_json(ctx.paths, report, ctx.backends, ctx.adapters);
  } else {
    print_status_text(ctx.paths, report, ctx.backends, ctx.adapters);
  }
  return 0;
}

}  // namespace hyprshare::cli
