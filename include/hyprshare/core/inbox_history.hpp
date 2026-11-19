#pragma once

#include <expected>
#include <cstddef>
#include <string>
#include <vector>

#include "hyprshare/model/types.hpp"
#include "hyprshare/platform/paths.hpp"

namespace hyprshare::core {

std::expected<model::InboxHistoryEntry, std::string> append_inbox_history(
  const model::InboxReceivedFile& received,
  const platform::Paths& paths);
std::expected<std::vector<model::InboxHistoryEntry>, std::string> list_inbox_history(
  const platform::Paths& paths);
std::expected<std::size_t, std::string> clear_inbox_history(
  const platform::Paths& paths);

}  // namespace hyprshare::core
