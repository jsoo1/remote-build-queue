/*
 * An implementation of the nix build-hook
 */

#include <algorithm>
#include <iostream>

#include <nix/local-fs-store.hh>
#include <nix/serialise.hh>

#include <dequeue.hh>
#include <enqueue/main.hh>
#include <job.hh>

using std::monostate;
using std::optional;
using std::ostringstream;
using std::set;
using std::shared_ptr;
using std::string;
using std::variant;
using std::vector;

using namespace nix;

using nlohmann::json;

namespace remote_build {
namespace enqueue {

/// Implements the hook side of the hook/build "protocol".
///
/// The other side of this protocol is another process sending
/// messages on stdin and receiving them on stderr.
///
/// As such, the order of reading from stdin/writing to stderr
/// matters and cannot be rearranged.
///
/// Similarly, \n is used instead of std::endl.
MainResult main(ConnectionParams const &conn_params) {
  auto input = std::make_shared<FdSource>(STDIN_FILENO);

  // First read settings from stdin
  auto const settings = get_settings(input);

  auto store = openStore();

  // Second, read build requirements from stdin
  auto reqs = build_requirements::get(input, store);

  // Send one line or exit if unwilling to take the job
  if (!reqs)
    return MainResult(monostate());

  auto conn_res = remote_build::postgres::connect(conn_params);

  if (std::holds_alternative<string>(conn_res))
    return MainResult{get<string>(conn_res)};

  auto conn_ = get<shared_ptr<PGconn>>(conn_res);

  auto ctx = std::make_shared<Ctx>(input, settings, conn_);

  auto enqueue_res = postgres::enqueue_job(ctx->conn.get(), *reqs);

  debug(concat_strings::sep(
      vector<string>{remote_build::postgres::show_conn_string(ctx->conn.get()),
                     "considering:", string(reqs->drv_path.to_string()) + ",",
                     (reqs->am_willing == 0 ? "no local jobs available,"
                                            : "local jobs available,"),
                     reqs->needed_system, "required features:",
                     concat_strings::sep(reqs->required_features, ",") + ","},
      " "));

  if (std::holds_alternative<string>(enqueue_res))
    return MainResult{get<string>(enqueue_res)};

  auto job_id = get<Uuid>(enqueue_res);

  auto interrupt_cb = nix::createInterruptCallback(
      [&ctx, &job_id]() { postgres::cancel_job(ctx->conn.get(), job_id); });

  auto events_res = dequeue::listen_channel(conn_params, job_id.val);

  if (std::holds_alternative<string>(events_res))
    return MainResult{get<string>(events_res)};

  auto events = get<dequeue::Events>(events_res);

  auto seed_events_res = dequeue::get_events(ctx->conn.get(), job_id);

  if (std::holds_alternative<string>(seed_events_res))
    return MainResult{get<string>(seed_events_res)};

  auto seed_events = get<std::queue<ListenResult>>(seed_events_res);

  auto events_iter = events.begin(std::move(seed_events));

  shared_ptr<event::Accept> accepted;

  while (true) {
    if (std::holds_alternative<dequeue::Error>(*events_iter->curr))
      return MainResult(
          dequeue::err_msg(get<dequeue::Error>(*events_iter->curr)));

    auto event = get<event::Event>(*events_iter->curr);

    // If there is no machine, send 'decline-permanently'
    if (std::holds_alternative<event::NoMachineAvailable>(event)) {
      std::cerr << "# decline-permanently\n";

      return MainResult(monostate());
    }

    // Send 'accept' on stderr
    if (std::holds_alternative<event::Accept>(event)) {
      std::cerr << "# accept\n";

      accepted = std::make_shared<event::Accept>(get<event::Accept>(event));

      goto accepted;
    }

    ++events_iter;
  }

accepted:

  // Send the hostname that accepted the job on stderr
  std::cerr << accepted->payload.uri << "\n";

  /// Get the inputs and expected outputs for the derivation
  /// after sending "accept" back
  //
  // TODO: Figure out the situation where the queue is running on another host
  // and inputs need to be copied to the remote machine.
  //
  // For instance, we could just openStore using an ssh connection to the remote
  // host using the machine uri we are going to get.
  //
  // As is, we just rely on the local store (aka the queue) to copy to the
  // remote.
  //
  // The reason for this is that one point of writing remote-build-queue is to
  // remove the upload lock on builders.
  auto inputs = nix::readStrings<nix::PathSet>(*ctx->input);

  auto wanted_outputs = nix::readStrings<nix::StringSet>(*ctx->input);

  auto add_inputs_and_outputs_res = postgres::add_inputs_and_outputs(
      ctx->conn.get(), accepted->job, store->parseStorePathSet(inputs),
      wanted_outputs);

  if (std::holds_alternative<string>(add_inputs_and_outputs_res))
    return MainResult(get<string>(add_inputs_and_outputs_res));

  events_iter++;

  while (true) {
  }

  return MainResult(monostate());
}

vector<NixSetting> get_settings(shared_ptr<FdSource> source) {
  vector<NixSetting> res;

  while (readInt(*source))
    res.emplace_back(NixSetting{
        .key = readString(*source),
        .val = readString(*source),
    });

  // TODO: Maybe need to `initPlugins`?
  return res;
}

} // namespace enqueue
} // namespace remote_build
