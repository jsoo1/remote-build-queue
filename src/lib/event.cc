#include <nix/util.hh>

#include <concat-strings.hh>
#include <event.hh>

using nix::fmt;
using nix::overloaded;

namespace remote_build {
namespace event {

ParseResult parse(Fields<json> const &fields) {
  try {
    if (fields.name == "start") {
      return ParseResult(Fields(fields, job::Job(fields.payload)));
    } else if (fields.name == "cancel") {
      return ParseResult(Fields(fields, C{}));
    } else if (fields.name == "no-machine-available") {
      return ParseResult(Fields(fields, N{}));
    } else if (fields.name == "accept") {
      return ParseResult(Fields(fields, JobMachine(fields.payload)));
    } else if (fields.name == "add-inputs-and-outputs") {
      return ParseResult(Fields(fields, InputsOutputs(fields.payload)));
    } else if (fields.name == "fail") {
      return ParseResult(Fields(fields, BuildError(fields.payload)));
    } else {
      return ParseResult(fmt("unexpected event name: %s", fields.name));
    }
  } catch (json::exception &e) {
    return ParseResult(fmt("parsing '%s': %s. got %s", fields.name, e.what(),
                           fields.payload.dump()));
  }
}

postgres::FromRowResult<Event> from_row(std::array<optional<string>, 4> tup) {
  auto f = Fields<json>::from_row(tup);

  if (std::holds_alternative<postgres::FromRowError>(f))
    return postgres::FromRowResult<Event>(get<postgres::FromRowError>(f));

  auto e = parse(get<Fields<json>>(f));

  if (std::holds_alternative<string>(e))
    return postgres::FromRowResult<Event>(
        postgres::FromRowError(get<string>(e)));

  return postgres::FromRowResult<Event>(get<Event>(e));
}

Fields<monostate> plain(Event const &event) {
  auto handle = overloaded{
      [](const Start &e) {
        return Fields<monostate>(e.ts, e.name, e.job.val, monostate());
      },
      [](const Cancel &e) {
        return Fields<monostate>(e.ts, e.name, e.job.val, monostate());
      },
      [](const NoMachineAvailable &e) {
        return Fields<monostate>(e.ts, e.name, e.job.val, monostate());
      },
      [](const Accept &e) {
        return Fields<monostate>(e.ts, e.name, e.job.val, monostate());
      },
      [](const AddInputsAndOutputs &e) {
        return Fields<monostate>(e.ts, e.name, e.job.val, monostate());
      },
      [](const Fail &e) {
        return Fields<monostate>(e.ts, e.name, e.job.val, monostate());
      },
  };

  return visit(handle, event);
}

} // namespace event
} // namespace remote_build
