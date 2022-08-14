#pragma once

#include <libpq-fe.h>

#include <queue>
#include <string>
#include <variant>

#include <dequeue.hh>
#include <enqueue/build-requirements.hh>
#include <uuid.hh>

using std::monostate;
using std::string;
using std::variant;

using remote_build::dequeue::ListenResult;
using remote_build::enqueue::build_requirements::BuildRequirements;
using remote_build::uuid::Uuid;

namespace remote_build {
namespace enqueue {
namespace postgres {

variant<string, Uuid> enqueue_job(PGconn *conn, BuildRequirements const &reqs);

variant<string, monostate> cancel_job(PGconn *conn, Uuid const &job);

variant<string, monostate>
add_inputs_and_outputs(PGconn *conn, Uuid const &job,
                       nix::StorePathSet const &inputs,
                       nix::StringSet const &wanted_outputs);

} // namespace postgres
} // namespace enqueue
} // namespace remote_build
