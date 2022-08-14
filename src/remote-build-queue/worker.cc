#include <memory>

#include <nix/build-result.hh>
#include <nix/derivations.hh>
#include <nix/legacy.hh>
#include <nix/store-api.hh>

#include <dequeue.hh>
#include <job.hh>
#include <remote-build-queue/postgres.hh>
#include <remote-build-queue/worker.hh>

using std::shared_ptr;

using nix::fmt;
using nix::get;
using nix::Verbosity::lvlVomit;

namespace remote_build {
namespace queue {
namespace worker {

void Worker::run(Wakeup &wakeup) {
  nix::ref<nix::Store> localStore = nix::openStore();

  while (true) {
    auto todo(this->todo.lock());

    todo.wait(inbox);

    auto conn_res = postgres::connect(this->conn_params);

    if (std::holds_alternative<string>(conn_res))
      return die(wakeup, get<string>(conn_res));

    auto listen_res =
        dequeue::listen_channel(this->conn_params, todo->get()->job.val);

    if (std::holds_alternative<string>(listen_res))
      return die(wakeup, get<string>(listen_res));

    auto events = get<dequeue::Events>(listen_res);

    auto accept_res =
        accept_job(this->conn.get(), todo->get()->job, this->machine->storeUri);

    if (std::holds_alternative<string>(accept_res))
      return die(wakeup, get<string>(accept_res));

    shared_ptr<event::AddInputsAndOutputs> inputs_outputs;

    auto events_iter = events.begin();

    while (true) {
      if (std::holds_alternative<dequeue::Error>(*events_iter->curr))
        return die(wakeup,
                   dequeue::err_msg(get<dequeue::Error>(*events_iter->curr)));

      auto event = get<event::Event>(*events_iter->curr);

      vomit("%s got event %s", this->machine->storeUri,
            event::plain(event).name);

      if (std::holds_alternative<event::AddInputsAndOutputs>(event)) {
        inputs_outputs = std::make_shared<event::AddInputsAndOutputs>(
            get<event::AddInputsAndOutputs>(event));

        goto dependencies_known;
      }

      ++events_iter;
    }

  dependencies_known:

    debug("copying dependencies to '%s'", this->machine->storeUri);

    auto substitute = nix::settings.buildersUseSubstitutes ? nix::Substitute
                                                           : nix::NoSubstitute;

    copyPaths(*localStore, *this->store, inputs_outputs->payload.inputs,
              nix::NoRepair, nix::NoCheckSigs, substitute);

    // TODO: Figure out the distributed case
    auto drv_path = nix::StorePath(todo->get()->payload.drv);

    auto drv = localStore->readDerivation(drv_path);

    auto output_hashes = staticOutputHashes(*localStore, drv);

    if (!drv.inputDrvs.empty())
      drv.inputSrcs = inputs_outputs->payload.inputs;

    auto result = this->store->buildDerivation(drv_path, drv);

    debug("emptying inbox of '%s'", this->machine->storeUri);

    todo->reset();
  }
}

void Worker::die(Wakeup &wakeup, nix::Error e) {
  wakeup.push(std::make_pair(machine.get(), e));
}

void Worker::die(Wakeup &wakeup, string const &msg) {
  wakeup.push(std::make_pair(machine.get(), nix::Error(msg)));
}

} // namespace worker
} // namespace queue
} // namespace remote_build
