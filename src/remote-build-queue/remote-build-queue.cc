#include <variant>

#include <nix/config.hh>
#include <nix/globals.hh>
#include <nix/shared.hh>

#include <remote-build-queue/main.hh>
#include <remote-build-queue/postgres.hh>

#include "args.hh"

using std::variant;

using nix::get;
using nix::Verbosity::lvlVomit;

inline remote_build::queue::Args args;

int main(int argc, char **argv) {
  return nix::handleExceptions(argv[0], [&]() {
    nix::initNix();

    args.parseCmdline(nix::argvToStrings(argc, argv));

    nix::logger = nix::makeSimpleLogger(true);

    auto conn_params = remote_build::queue::env_conn_params(nix::getEnv());

    if (std::holds_alternative<string>(conn_params))
      throw nix::UsageError(get<string>(conn_params));

    remote_build::queue::main(
        get<remote_build::postgres::ConnectionParams>(conn_params));

    return EXIT_SUCCESS;
  });
}
