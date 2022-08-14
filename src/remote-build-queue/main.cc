#include <algorithm>
#include <memory>

#include <nix/local-fs-store.hh>
#include <nix/logging.hh>
#include <nix/ref.hh>

#include <nlohmann/json.hpp>

#include <job.hh>
#include <remote-build-queue/main.hh>
#include <remote-build-queue/postgres.hh>
#include <remote-build-queue/worker.hh>

using nix::fmt;
using nix::get;
using nix::logger;
using nix::overloaded;
using nix::Verbosity::lvlDebug;
using nix::Verbosity::lvlError;
using nix::Verbosity::lvlVomit;

using nlohmann::json;

namespace remote_build {
namespace queue {

void main(postgres::ConnectionParams const &conn_params) {
  assert(PQisthreadsafe());

  // Avoid asking for ssh creds on stdin when using ssh store connections
  unsetenv("DISPLAY");

  unsetenv("SSH_ASKPASS");

  nix::ref<State> state(std::make_unique<State>(conn_params));

  debug("machine priorities:");

  for (auto &slot : state->ready)
    debug(machines::show(*slot->machine.get()));

  auto events_buf = Buffer<ListenResult *>{};

  thread([&state, &events_buf]() {
    collect_events(state, events_buf);
  }).detach();

  thread([&state, &events_buf]() { listen_queue(state, events_buf); }).detach();

  auto wake_workers = worker::Wakeup();

  thread([&state, &wake_workers]() {
    auto [dead_machine, err] = wake_workers.pop();

    debug("got exception from worker %s", dead_machine->storeUri);

    auto exc(state->exc_.lock());

    *exc = err;

    state->fatal.notify_one();
  }).detach();

  for (auto &worker : state->ready) {
    thread([&worker, &wake_workers]() { worker->run(wake_workers); }).detach();
  }

  auto exc(state->exc_.lock());

  exc.wait(state->fatal);

  debug("shutting down");

  if (*exc) {
    // For some reason nix::handleExceptions hangs waiting to shut down threads.
    // Otherwise it would be cool to just rethrow **exc
    printError(exc->value().msg());
    exit(1);
  }
}

void listen_queue(nix::ref<State> &state, Buffer<ListenResult *> &buf) {
  auto listen_res = dequeue::listen_channel(state->conn_params, "events");

  if (std::holds_alternative<string>(listen_res))
    return quit(state, nix::Error(get<string>(listen_res)));

  auto events = get<Events>(listen_res);

  auto events_iter = events.begin();

  while (true) {
    buf.push(events_iter->curr.get());

    ++events_iter;
  }
}

void collect_events(nix::ref<State> &state, Buffer<ListenResult *> &buf) {
  auto conn_res = postgres::connect(state->conn_params);

  if (std::holds_alternative<string>(conn_res))
    return quit(state, nix::Error(get<string>(conn_res)));

  auto conn = get<shared_ptr<PGconn>>(conn_res);

  auto handle_result = overloaded{
      [&state, &conn](Event const &event) {
        handle_event(state, conn.get(), event);
      },
      [&state, &conn](dequeue::Error const &err) {
        handle_err(state, conn.get(), err);
      },
  };

  while (true) {
    visit(handle_result, *buf.pop());
  }
}

void handle_event(nix::ref<State> &state, PGconn *conn,
                  event::Event const &event) {
  auto handler = overloaded{
      [&](event::Start const &start) {
        auto job_res = job::get(conn, start.job);

        if (std::holds_alternative<string>(job_res)) {
          printError(get<string>(job_res));

          return;
        }

        auto slot = std::find_if(state->ready.begin(), state->ready.end(),
                                 [&](shared_ptr<Worker> const &worker) {
                                   auto worker_curr(worker->todo.lock());

                                   return !*worker_curr &&
                                          machines::can_build(*worker->machine,
                                                              start.payload);
                                 });

        if (slot == state->ready.end()) {
          vomit("rejecting job %s, no machine available", start.job.val);

          auto deny_res = no_machine_available(conn, start.job);

          if (std::holds_alternative<string>(deny_res))
            return quit(state, nix::Error(get<string>(deny_res)));

        } else {
          auto worker = *slot;

          auto worker_job(worker->todo.lock());

          *worker_job = std::make_unique<event::Start>(start);

          worker->inbox.notify_one();
        }
      },
      [](event::Cancel const &e) {},
      [](event::Accept const &e) {},
      [](event::NoMachineAvailable const &e) {},
      [](event::AddInputsAndOutputs const &e) {},
      [](event::Fail const &e) {},
  };

  visit(handler, event);
}

void handle_err(nix::ref<State> &state, PGconn *conn,
                dequeue::Error const &err) {
  printError(dequeue::err_msg(err));

  if (std::holds_alternative<dequeue::PollingError>(err)) {
    return quit(state, err);
  }

  if (std::holds_alternative<dequeue::ConsumingInput>(err)) {
    return quit(state, err);
  }
}

void quit(nix::ref<State> &state, nix::Error const &e) {
  auto exc(state->exc_.lock());

  *exc = e;

  state->fatal.notify_one();
}

void quit(nix::ref<State> &state, dequeue::Error const &e) {
  quit(state, nix::Error(dequeue::err_msg(e)));
}

} // namespace queue
} // namespace remote_build
