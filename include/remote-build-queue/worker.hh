#pragma once

#include <condition_variable>
#include <optional>
#include <queue>
#include <utility>
#include <variant>

#include <nix/logging.hh>
#include <nix/machines.hh>
#include <nix/ref.hh>
#include <nix/store-api.hh>
#include <nix/sync.hh>
#include <nix/util.hh>

#include <dequeue.hh>
#include <event.hh>
#include <job.hh>
#include <postgres.hh>
#include <remote-build-queue/machines.hh>

using std::condition_variable;
using std::monostate;
using std::optional;
using std::pair;
using std::shared_ptr;
using std::unique_ptr;
using std::variant;

using nix::fmt;
using nix::get;
using nix::logger;
using nix::overloaded;
using nix::Sync;
using nix::Verbosity::lvlDebug;
using nix::Verbosity::lvlError;

using remote_build::dequeue::Buffer;

namespace remote_build {
namespace queue {
namespace worker {

typedef Buffer<pair<nix::Machine *, nix::Error>> Wakeup;

struct Worker {
private:
  shared_ptr<nix::AutoCloseFD> write_ssh;

public:
  const postgres::ConnectionParams conn_params;
  shared_ptr<nix::Machine> machine;
  shared_ptr<nix::AutoCloseFD> read_ssh;
  shared_ptr<nix::Store> store;
  shared_ptr<PGconn> conn;
  Sync<unique_ptr<event::Start>> todo;
  condition_variable inbox;

  Worker(postgres::ConnectionParams const &conn_params,
         shared_ptr<nix::Machine> const machine)
      : write_ssh(), conn_params(conn_params), machine(machine), read_ssh(),
        store(), conn(),
        todo(Sync<unique_ptr<event::Start>>(unique_ptr<event::Start>{})),
        inbox() {
    debug("connecting to store: %s", machine->storeUri);

    nix::Pipe ssh_pipe;

    ssh_pipe.create();

    try {
      store = machines::open_store(*machine, ssh_pipe);

      read_ssh =
          std::make_shared<nix::AutoCloseFD>(std::move(ssh_pipe.readSide));

      write_ssh =
          std::make_shared<nix::AutoCloseFD>(std::move(ssh_pipe.writeSide));

      store->connect();

    } catch (std::exception &e) {
      auto msg = read_ssh ? nix::drainFD(read_ssh->get(), false) : "";

      throw nix::Error("%s%s", e.what(), msg.empty() ? "" : ": " + msg);
    }

    auto conn_res = postgres::connect(conn_params);

    if (std::holds_alternative<string>(conn_res))
      throw nix::Error(get<string>(conn_res));

    conn = get<shared_ptr<PGconn>>(conn_res);
  }

  void run(Wakeup &wakeup);

  void die(Wakeup &wakeup, nix::Error e);

  void die(Wakeup &wakeup, string const &msg);
};

} // namespace worker
} // namespace queue
} // namespace remote_build
