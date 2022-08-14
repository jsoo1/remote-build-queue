#pragma once

#include <memory>
#include <optional>
#include <set>
#include <string>

#include <nix/store-api.hh>
#include <nix/util.hh>

using std::optional;
using std::set;
using std::shared_ptr;
using std::string;

namespace remote_build {
namespace enqueue {
namespace build_requirements {

struct BuildRequirements {
  unsigned int am_willing;
  string needed_system;
  nix::StorePath drv_path;
  set<string> required_features;
};

optional<BuildRequirements> get(shared_ptr<nix::FdSource> input,
                                nix::ref<nix::Store> store);

} // namespace build_requirements
} // namespace enqueue
} // namespace remote_build
