/*
 * An implementation of the nix build-hook that enqueues jobs onto a postgres
 * queue.
 */

#include <nix/config.hh>
#include <nix/globals.hh>
#include <nix/shared.hh>

#include <enqueue/main.hh>

int main(int argc, char const *const *argv) {
  return nix::handleExceptions(argv[0], [&]() {
    nix::initNix();

    nix::logger = makeJSONLogger(*nix::logger);

    if (argc != 6)
      throw nix::UsageError(
          "called without required arguments. USAGE: build-hook = "
          "enqueue user host port database");

    nix::verbosity = (nix::Verbosity)std::stoll(argv[5]);

    // The build hook gets exec'd in an odd way, so argv is a little jumbled
    // See @nix/src/build-remote/build-remote.{hh,cc}
    // and @nix/src/libstore/build/hook-instance.{hh,cc}
    auto conn_params = ConnectionParams{
        .user = string(argv[0]),
        .host = string(argv[1]),
        .port = string(argv[2]),
        .dbname = string(argv[3]),
    };

    auto res = remote_build::enqueue::main(conn_params);

    auto succeed_or_die = overloaded{
        [&](monostate x) { return; },
        [&](string err) { throw nix::Error(err); },
    };

    return visit(succeed_or_die, res);
  });
}
