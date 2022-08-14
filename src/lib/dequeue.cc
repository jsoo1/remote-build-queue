#include <memory>
#include <nix/logging.hh>

#include <dequeue.hh>

using nix::fmt;
using nix::get;
using nix::logger;
using nix::Verbosity::lvlVomit;

namespace remote_build {
namespace dequeue {

string err_msg(Error e) {
  auto handle = overloaded{
      [](dequeue::PollingError e) { return e.msg(); },
      [](dequeue::ConsumingInput e) { return e.msg(); },
      [](dequeue::JsonDecodeError e) { return e.msg(); },
      [](dequeue::ParsingEvent e) { return e.msg(); },
      [](dequeue::WrongChannel e) { return e.msg(); },
      [](dequeue::EscapingChannel e) { return e.msg(); },
      [](dequeue::NoMessages e) { return e.msg; },
  };

  return visit(handle, e);
}

variant<string, Events>
listen_channel(postgres::ConnectionParams const &conn_params,
               const string &channel_ident) {

  auto conn_res = postgres::connect(conn_params);

  if (std::holds_alternative<string>(conn_res))
    return variant<string, Events>(get<string>(conn_res));

  auto conn = get<shared_ptr<PGconn>>(conn_res);

  auto channel_res =
      postgres::escape_identifier(conn.get(), string(channel_ident));

  if (std::holds_alternative<string>(channel_res))
    return variant<string, Events>(get<string>(channel_res));

  auto channel = get<shared_ptr<char>>(channel_res);

  auto listen_stmt = "LISTEN " + string(channel.get());

  auto res = postgres::exec(conn.get(), listen_stmt.data());

  if (PQresultStatus(res.get()) != PGRES_COMMAND_OK)
    return variant<string, Events>(
        postgres::err_msg(res.get(), "listening to " + string(channel.get())));

  else
    return variant<string, Events>(Events(std::move(conn), channel_ident));
}

NonEmptyListenResults await_events(PGconn *conn, string const &on_channel) {
  auto poll_res = postgres::poll_socket_ready(conn);

  if (std::holds_alternative<postgres::PollingError>(poll_res))
    return NonEmptyListenResults::singleton(ListenResult(
        Error(PollingError(get<postgres::PollingError>(poll_res)))));

  if (PQconsumeInput(conn) == 0)
    return NonEmptyListenResults::singleton(ListenResult(
        Error(ConsumingInput(postgres::err_msg(conn, "consuming input")))));

  std::queue<ListenResult> res;

  for (auto &notification : postgres::collect_notifications(conn)) {
    auto channel = string(notification->relname);

    vomit("got notification on %s, from pid %d", channel, notification->be_pid);

    if (channel == on_channel)
      try {
        event::Fields<json> fields(json::parse(notification->extra));

        auto evt = event::parse(fields);

        if (std::holds_alternative<string>(evt))
          res.push(ListenResult(Error(ParsingEvent(get<string>(evt)))));

        else
          res.push(ListenResult(get<event::Event>(evt)));

      } catch (json::exception &e) {
        res.push(ListenResult(Error(
            JsonDecodeError(string(notification->extra),
                            "failed decoding event: " + string(e.what())))));
      }

    else
      res.push(ListenResult(Error(WrongChannel(channel))));
  }

  if (res.empty()) {
    return NonEmptyListenResults::singleton(ListenResult(Error(NoMessages{})));

  } else {
    auto head = res.front();

    res.pop();

    return NonEmptyListenResults{
        .head = head,
        .tail = res,
    };
  }
}

GetEventsResult get_events(PGconn *conn, Uuid const &job) {
  auto id = postgres::escape_uuid(job);

  char *params[1] = {id.data()};

  auto res = postgres::exec_params(
      conn, "SELECT * FROM ROWS FROM (@schema@.get_events($1::uuid))", 1,
      params);

  if (PQresultStatus(res.get()) != PGRES_TUPLES_OK)
    return GetEventsResult(postgres::err_msg(res.get(), "getting events"));

  auto query_res =
      postgres::collect_tuples<event::Event, 4>(res.get(), event::from_row);

  auto result = std::queue<ListenResult>();

  for (auto &row : query_res) {
    if (std::holds_alternative<postgres::FromRowError>(row))
      return GetEventsResult(std::get<postgres::FromRowError>(row).msg);

    auto e = std::get<event::Event>(row);

    result.push(e);
  }

  return GetEventsResult(result);
}

} // namespace dequeue
} // namespace remote_build
