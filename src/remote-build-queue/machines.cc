#include <algorithm>
#include <string>
#include <vector>

#include <nix/store-api.hh>
#include <nix/util.hh>

#include <concat-strings.hh>
#include <remote-build-queue/machines.hh>

namespace remote_build {
namespace queue {
namespace machines {

nix::ref<nix::Store> open_store(nix::Machine const &machine,
                                nix::Pipe &ssh_pipe) {
  nix::Store::Params storeParams;
  if (nix::hasPrefix(machine.storeUri, "ssh://")) {
    storeParams["max-connections"] = "1";
    storeParams["log-fd"] = nix::fmt("%d", ssh_pipe.writeSide.get());
  }

  if (nix::hasPrefix(machine.storeUri, "ssh://") ||
      nix::hasPrefix(machine.storeUri, "ssh-ng://")) {
    if (machine.sshKey != "")
      storeParams["ssh-key"] = machine.sshKey;
    if (machine.sshPublicHostKey != "")
      storeParams["base64-ssh-public-host-key"] = machine.sshPublicHostKey;
  }

  auto feats = vector{
      concat_strings::sep(machine.supportedFeatures, " "),
      concat_strings::sep(machine.mandatoryFeatures, " "),
  };

  storeParams["system-features"] = concat_strings::sep(feats, " ");

  return nix::openStore(machine.storeUri, storeParams);
}

/// Relies on systems being sorted/unique on load,
bool priority_lt(const shared_ptr<nix::Machine> &a,
                 const shared_ptr<nix::Machine> &b) {
  if (a->systemTypes < b->systemTypes)
    return false;

  if (b->mandatoryMet(a->mandatoryFeatures))
    return false;

  if (a->allSupported(b->supportedFeatures))
    return false;

  if (a->speedFactor < b->speedFactor)
    return false;

  if (a->maxJobs < b->maxJobs)
    return false;

  if (a->storeUri < b->storeUri)
    return false;

  if (a->sshPublicHostKey < b->sshPublicHostKey)
    return false;

  if (a->sshKey < b->sshKey)
    return false;

  return true;
}

nix::Machine sort_unique_system_types(const nix::Machine &m) {
  vector<std::string> system_types(m.systemTypes);

  std::sort(system_types.begin(), system_types.end());

  auto last = std::unique(system_types.begin(), system_types.end());

  system_types.erase(last, system_types.end());

  return nix::Machine(
      std::string(m.storeUri), system_types, std::string(m.sshKey), m.maxJobs,
      m.speedFactor, std::set(m.supportedFeatures),
      std::set(m.mandatoryFeatures), std::string(m.sshPublicHostKey));
};

std::string show(nix::Machine const &machine) {
  auto strings = std::vector<string>{
      machine.storeUri,
      concat_strings::sep(machine.systemTypes, ","),
      machine.sshKey,
      nix::fmt("%d", machine.maxJobs),
      nix::fmt("%d", machine.speedFactor),
      concat_strings::sep(machine.supportedFeatures, ","),
      concat_strings::sep(machine.mandatoryFeatures, ","),
      machine.sshPublicHostKey,
  };

  return concat_strings::sep(strings, " ");
}

bool can_build(nix::Machine const &machine, job::Job const &todo) {
  return (todo.system == "builtin" ||
          std::find(machine.systemTypes.begin(), machine.systemTypes.end(),
                    todo.system) != machine.systemTypes.end()) &&
         machine.allSupported(todo.system_features) &&
         machine.mandatoryMet(todo.system_features);
}

} // namespace machines
} // namespace queue
} // namespace remote_build
