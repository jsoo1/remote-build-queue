#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>

#include <libpq-fe.h>

#include <nix/sync.hh>
#include <nix/util.hh>

#include <dequeue.hh>
#include <enqueue/build-requirements.hh>
#include <enqueue/postgres.hh>
#include <event.hh>
#include <postgres.hh>
#include <uuid.hh>

using std::monostate;
using std::optional;
using std::ostringstream;
using std::set;
using std::shared_ptr;
using std::string;
using std::variant;
using std::vector;

using nix::FdSource;
using nix::Sync;

using remote_build::enqueue::build_requirements::BuildRequirements;
using remote_build::postgres::ConnectionParams;

namespace remote_build {
namespace enqueue {

// The build-hook is exec'd with two extra fds
// to collect logs from the build machine.
// See @nix/src/libstore/build/hook-instance.cc
//
// TODO: Understand this. I flipped these for the time
// being because they are the opposite from the hook-instance'
// perspective.
const int BUILDER_OUT_READ = 4;

const int BUILDER_OUT_WRITE = 5;

struct NixSetting {
  string key;
  string val;
};

struct Ctx {
  shared_ptr<FdSource> input;
  vector<NixSetting> const settings;
  shared_ptr<PGconn> conn;

  Ctx(shared_ptr<FdSource> i, vector<NixSetting> const s,
      shared_ptr<PGconn> conn)
      : input(i), settings(s), conn(conn){};

  Ctx(Ctx const &c) = default;
};

vector<NixSetting> get_settings(shared_ptr<FdSource> source);

typedef variant<string, monostate> MainResult;

MainResult main(ConnectionParams const &conn_params);

} // namespace enqueue
} // namespace remote_build
