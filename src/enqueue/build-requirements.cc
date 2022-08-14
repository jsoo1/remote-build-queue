#include <nix/local-fs-store.hh>

#include <enqueue/build-requirements.hh>

using std::optional;

namespace remote_build {
namespace enqueue {
namespace build_requirements {

optional<BuildRequirements> get(shared_ptr<nix::FdSource> input,
                                nix::ref<nix::Store> store) {
  try {
    auto s = nix::readString(*input);
    if (s != "try")
      return std::nullopt;
  } catch (nix::EndOfFile &) {

    return std::nullopt;
  }

  auto localStore = store.dynamic_pointer_cast<nix::LocalFSStore>();

  return optional<BuildRequirements>(BuildRequirements{
      .am_willing = nix::readInt(*input),
      .needed_system = nix::readString(*input),
      .drv_path = localStore->parseStorePath(nix::readString(*input)),
      .required_features = nix::readStrings<set<string>>(*input),
  });
}

} // namespace build_requirements
} // namespace enqueue
} // namespace remote_build
