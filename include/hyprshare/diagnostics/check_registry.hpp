#pragma once

#include <functional>
#include <string>
#include <vector>

#include "hyprshare/model/types.hpp"
#include "hyprshare/platform/paths.hpp"
#include "hyprshare/platform/process.hpp"

namespace hyprshare::diagnostics {

using CheckFunction = std::function<model::CheckResult(const platform::Paths&, const platform::ProcessRunner&)>;

class CheckRegistry {
 public:
  void register_check(std::string id, CheckFunction check);
  std::vector<model::CheckResult> run_all(const platform::Paths& paths, const platform::ProcessRunner& runner) const;

 private:
  struct Entry {
    std::string id;
    CheckFunction check;
  };
  std::vector<Entry> checks_;
};

}  // namespace hyprshare::diagnostics
