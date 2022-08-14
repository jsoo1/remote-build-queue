#include <algorithm>
#include <condition_variable>
#include <memory>
#include <optional>
#include <queue>
#include <set>
#include <shared_mutex>
#include <thread>
#include <utility>

#include <nix/sync.hh>
#include <nix/util.hh>

#include <dequeue.hh>
#include <event.hh>
#include <postgres.hh>
#include <remote-build-queue/machines.hh>
#include <remote-build-queue/worker.hh>

using std::condition_variable;
using std::optional;
using std::pair;
using std::priority_queue;
using std::set;
using std::shared_ptr;
using std::string;
using std::thread;
using std::unique_ptr;
using std::variant;

using nix::Sync;

using remote_build::dequeue::Buffer;
using remote_build::dequeue::Events;
using remote_build::dequeue::ListenResult;
using remote_build::event::Event;
using remote_build::queue::worker::Worker;

namespace remote_build {
namespace queue {

typedef priority_queue<event::Start, std::vector<event::Start>,
                       event::OrdByTimeAsc<job::Job>>
    WaitQueue;

typedef vector<shared_ptr<Worker>> Slots;

struct State {
  const postgres::ConnectionParams conn_params;
  WaitQueue waiting;
  Slots ready;
  Slots busy;
  Sync<optional<nix::Error>> exc_;
  condition_variable fatal;

  State(postgres::ConnectionParams const &conn_params)
      : conn_params(conn_params), waiting(), ready(), busy(), exc_(), fatal() {

    auto machines = nix::getMachines();

    auto mk_slots = [&conn_params](nix::Machine const &m) {
      auto mach =
          std::make_shared<nix::Machine>(machines::sort_unique_system_types(m));

      return std::make_shared<Worker>(conn_params, mach);
    };

    std::transform(machines.begin(), machines.end(), std::back_inserter(ready),
                   mk_slots);

    std::sort(ready.begin(), ready.end(),
              [](shared_ptr<Worker> const &a, shared_ptr<Worker> const &b) {
                return machines::priority_lt(a->machine, b->machine);
              });
  }
};

void main(postgres::ConnectionParams const &conn_params);

void quit(nix::ref<State> &state, nix::Error const &e);

void quit(nix::ref<State> &state, dequeue::Error const &e);

void listen_queue(nix::ref<State> &state, Buffer<ListenResult *> &buf);

void collect_events(nix::ref<State> &state, Buffer<ListenResult *> &buf);

void handle_event(nix::ref<State> &state, PGconn *conn, Event const &event);

void handle_err(nix::ref<State> &state, PGconn *conn,
                dequeue::Error const &err);

} // namespace queue
} // namespace remote_build
