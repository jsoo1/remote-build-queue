#pragma once

#include <nix/common-args.hh>

namespace remote_build {
namespace queue {

// TODO: Handle environment variables for pg
// non-virtual-dtor: Safe to ignore - the args will be static.
// missing-field-initializers: Unused argument fields.
#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#elif __clang__
#pragma clang diagnostic ignored "-Wnon-virtual-dtor"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif
struct Args : nix::MixCommonArgs {
  Args() : nix::MixCommonArgs("remote-build-queue") {}

  ~Args() {}
};
#ifdef __GNUC__
#pragma GCC diagnostic warning "-Wnon-virtual-dtor"
#pragma GCC diagnostic warning "-Wmissing-field-initializers"
#elif __clang__
#pragma clang diagnostic warning "-Wnon-virtual-dtor"
#pragma clang diagnostic warning "-Wmissing-field-initializers"
#endif

} // namespace queue
} // namespace remote_build
