#pragma once

#include <algorithm>
#include <memory>
#include <set>
#include <vector>

#include <nix/machines.hh>

#include <job.hh>

using std::shared_ptr;

namespace remote_build {
namespace queue {
namespace machines {

// TODO: Upstream, maybe
// Like nix::Machine::openStore() but don't hard-code
// file-descriptors that the build hook-instance uses.
nix::ref<nix::Store> open_store(nix::Machine const &machine,
                                nix::Pipe &ssh_pipe);

// TODO: Upstream, maybe
// Like nix::Machine::openStore() but don't hard-code
// file-descriptors that the build hook-instance uses.
nix::ref<nix::Store> open_store(nix::Machine const &machine,
                                nix::Pipe &ssh_pipe);

/// Relies on systems being sorted/unique on load!
bool priority_lt(const shared_ptr<nix::Machine> &a,
                 const shared_ptr<nix::Machine> &b);

nix::Machine sort_unique_system_types(const nix::Machine &m);

std::string show(nix::Machine const &machine);

bool can_build(nix::Machine const &machine, job::Job const &todo);

} // namespace machines
} // namespace queue
} // namespace remote_build
