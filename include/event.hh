#pragma once

#include <string>
#include <variant>

#include <nlohmann/json.hpp>

#include <job.hh>
#include <postgres.hh>
#include <uuid.hh>

using std::monostate;
using std::string;
using std::variant;

using nix::get;

using nlohmann::json;

using remote_build::uuid::Uuid;

namespace remote_build {
namespace event {

template <class T> struct Fields {
  string ts;
  string name;
  Uuid job;
  T payload;

  Fields(const Fields<T> &fields) = default;

  Fields(const Fields<json> fields, const T &payload)
      : ts(fields.ts), name(fields.name), job(fields.job), payload(payload) {}

  Fields(json const &j)
      : ts(j["ts"]), name(j["name"]), job(Uuid(j["job"])),
        payload(j["payload"]) {}

  Fields(string const &ts, string const &name, string const &job,
         const T &payload)
      : ts(ts), name(name), job(Uuid(job)), payload(payload) {}

  static postgres::FromRowResult<Fields<json>>
  from_row(std::array<optional<string>, 4> tup) {
    auto [ts, name, job, payload] = tup;

    vector<string> errs;

    if (!ts.has_value())
      errs.push_back("ts was null");

    if (!name.has_value())
      errs.push_back("name was null");

    if (!job.has_value())
      errs.push_back("job was null");

    if (!payload.has_value())
      errs.push_back("payload was null");

    if (!errs.empty())
      return postgres::FromRowResult<Fields>(
          postgres::FromRowError(concat_strings::sep(errs, ", ")));

    try {
      return postgres::FromRowResult<Fields>(
          Fields(*ts, *name, *job, json::parse(*payload)));
    } catch (json::exception &e) {
      return postgres::FromRowError(
          nix::fmt("parsing payload: %s. got %s", e.what(), *payload));
    }
  }
};

using Start = Fields<job::Job>;

struct C {};

using Cancel = Fields<C>;

struct N {};

using NoMachineAvailable = Fields<N>;

struct JobMachine {
  string uri;
  JobMachine(const json &j) : uri(j["uri"]) {}
};

using Accept = Fields<JobMachine>;

struct InputsOutputs {
  nix::StorePathSet inputs;
  nix::StringSet wanted_outputs;

  InputsOutputs(const json &j) : inputs(), wanted_outputs(j["wanted_outputs"]) {
    vector<string> ins = j["inputs"];

    std::transform(
        ins.begin(), ins.end(),
        std::inserter(this->inputs, this->inputs.begin()),
        [](const string &p) { return nix::StorePath(std::string_view(p)); });
  }
};

using AddInputsAndOutputs = Fields<InputsOutputs>;

struct BuildError {
  string msg;
  BuildError(const json &j) : msg(j["msg"]) {}
};

using Fail = Fields<BuildError>;

typedef variant<Start, Cancel, NoMachineAvailable, Accept, AddInputsAndOutputs,
                Fail>
    Event;

typedef variant<string, Event> ParseResult;

ParseResult parse(Fields<json> const &fields);

postgres::FromRowResult<Event> from_row(std::array<optional<string>, 4> tup);

Fields<monostate> plain(Event const &event);

template <class T> struct OrdByTimeAsc {
  bool comp(Fields<T> const &a, Fields<T> const &b) { return a.ts < b.ts; }

  bool equiv(Fields<T> const &a, Fields<T> const &b) { return a.ts == b.ts; }
};

} // namespace event
} // namespace remote_build
