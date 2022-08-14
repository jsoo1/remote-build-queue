#pragma once

#include <queue>
#include <set>
#include <string>
#include <variant>

#include <libpq-fe.h>

#include <nlohmann/json.hpp>

#include <nix/store-api.hh>
#include <nix/types.hh>

#include <uuid.hh>

using std::set;
using std::string;
using std::variant;

using nlohmann::json;

using remote_build::uuid::Uuid;

namespace remote_build {
namespace job {

struct Job {
  string drv;
  string system;
  set<string> system_features;

  Job(string const &drv, string const &system,
      set<string> const &system_features)
      : drv(drv), system(system), system_features(system_features) {}

  Job(const json &j)
      : drv(j["drv"]), system(j["system"]),
        system_features(j["system_features"]) {}
};

variant<string, Job> from_postgres(PGresult *res);

variant<string, Job> get(PGconn *conn, Uuid const &id);

} // namespace job
} // namespace remote_build
