#include <cerrno>
#include <cstring>
#include <iostream>

#include <sys/poll.h>
#include <sys/types.h>

#include <concat-strings.hh>
#include <postgres.hh>

using std::monostate;
using std::ostringstream;
using std::variant;

namespace remote_build {
namespace postgres {

variant<string, shared_ptr<PGconn>> connect(ConnectionParams const &params) {
  char const *user_key = "user";
  char const *host_key = "host";
  char const *port_key = "port";
  char const *dbname_key = "dbname";

  char const *conn_keys[5] = {user_key, host_key, port_key, dbname_key, NULL};

  char const *conn_vals[5] = {params.user.c_str(), params.host.c_str(),
                              params.port.c_str(), params.dbname.c_str(), NULL};

  auto conn =
      shared_ptr<PGconn>(PQconnectdbParams(conn_keys, conn_vals, 0), PQfinish);

  if (PQstatus(conn.get()) != CONNECTION_OK)
    return variant<string, shared_ptr<PGconn>>{
        err_msg(conn.get(), "connecting to postgres")};

  auto res = exec(conn.get(),
                  "SELECT pg_catalog.set_config('search_path', '', false)");

  if (PQresultStatus(res.get()) != PGRES_TUPLES_OK)
    return variant<string, shared_ptr<PGconn>>(
        err_msg(res.get(), "setting secure search path"));

  return variant<string, shared_ptr<PGconn>>(conn);
}

string show_conn_string(PGconn *conn) {
  auto conn_info =
      shared_ptr<PQconninfoOption>(PQconninfo(conn), PQconninfoFree);

  if (conn_info == NULL) // ENOMEM
    exit(1);

  struct {
    string user;
    string host;
    string port;
    string dbname;
  } show_conn = {
      .user = "unknown",
      .host = "unknown",
      .port = "unknown",
      .dbname = "unknown",
  };

  auto param = conn_info.get();

  while (param->keyword != NULL) {
    if (string(param->keyword) == "user" && param->val != NULL) {
      show_conn.user = string(param->val);

    } else if (string(param->keyword) == "host" && param->val != NULL) {
      show_conn.host = string(param->val);

    } else if (string(param->keyword) == "port" && param->val != NULL) {
      show_conn.port = string(param->val);

    } else if (string(param->keyword) == "dbname" && param->val != NULL) {
      show_conn.dbname = string(param->val);
    }

    param++;
  }

  return "postgres://" + show_conn.user + "@" + show_conn.host + ":" +
         show_conn.port + "/" + show_conn.dbname;
}

shared_ptr<PGresult> exec(PGconn *conn, string const &stmt) {
  return shared_ptr<PGresult>(PQexec(conn, string(stmt).data()), PQclear);
}

shared_ptr<PGresult> exec_params(PGconn *conn, string const &stmt, int n_params,
                                 char **params) {
  return shared_ptr<PGresult>(PQexecParams(conn, string(stmt).data(), n_params,
                                           NULL, params, NULL, NULL, 0),
                              PQclear);
}

variant<string, shared_ptr<char>> escape_identifier(PGconn *conn,
                                                    string const &ident) {
  auto tmp = PQescapeIdentifier(conn, ident.data(), ident.length());

  if (tmp == NULL) {
    PQfreemem(tmp);
    return variant<string, shared_ptr<char>>(
        err_msg(conn, "escaping identifier " + ident));
  }

  return variant<string, shared_ptr<char>>(shared_ptr<char>(tmp, PQfreemem));
}

string err_msg(PGconn *conn, string const &additional_msg) {
  return PQerrorMessage(conn) + additional_msg;
}

string err_msg(PGconn *conn) { return err_msg(conn, ""); }

string err_msg(PGresult *res, string const &additional_msg) {
  return PQresultErrorMessage(res) + additional_msg;
}

string err_msg(PGresult *res) { return err_msg(res, ""); }

string err_msg(PollingError err) {
  auto show_err = overloaded{
      [](PollingConnectionClosed e) { return e.msg; },
      [](PollingFailed e) { return e.msg(); },
      [](PollingTimedOut e) { return e.msg; },
      [](PollingUnexpectedNumResults e) { return e.msg(); },
      [](PollingPollErr e) { return e.msg; },
      [](PollingPollHup e) { return e.msg; },
      [](PollingPollInval e) { return e.msg; },
  };
  return visit(show_err, err);
};

string to_sql_array(vector<string> const &v) {
  return "{" + concat_strings::sep(v, ",") + "}";
}

string to_sql_array(set<string> const &v) {
  return "{" + concat_strings::sep(v, ",") + "}";
}

string to_sql_array(nix::StorePathSet const &s) {
  vector<string> v;

  std::transform(s.begin(), s.end(), std::back_inserter(v),
                 [](nix::StorePath const &p) { return string(p.to_string()); });

  return "{" + concat_strings::sep(v, ",") + "}";
}

string escape_uuid(Uuid const &u) { return "{" + u.val + "}"; }

PollingResult poll_socket_ready(PGconn *conn) {
  auto sock = PQsocket(conn);

  if (sock < 0)
    return PollingResult(PollingError(PollingConnectionClosed{}));

  auto events = POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI;

  pollfd to_poll;

  to_poll.fd = sock;

  to_poll.events = events;

  to_poll.revents = {};

  pollfd poll_fds[1] = {to_poll};

  auto n_poll_read = poll(poll_fds, 1, -1);

  switch (n_poll_read) {
  case -1:
    return PollingResult(PollingError(PollingFailed(strerror(errno))));

  case 0:
    return PollingResult(PollingError(PollingTimedOut{}));

  case 1:
    break;

  default:
    return PollingResult(
        PollingError(PollingUnexpectedNumResults(n_poll_read)));
  }

  if (to_poll.revents & POLLERR) {
    return PollingResult(PollingError(PollingPollErr{}));

  } else if (to_poll.revents & POLLHUP) {
    return PollingResult(PollingError(PollingPollHup{}));

  } else if (to_poll.revents & POLLNVAL) {
    return PollingResult(PollingError(PollingPollInval{}));

  } else {
    return PollingResult(monostate());
  }
}

vector<shared_ptr<PGnotify>> collect_notifications(PGconn *conn) {
  vector<shared_ptr<PGnotify>> results;

  while (auto notification = PQnotifies(conn)) {
    results.emplace_back(shared_ptr<PGnotify>(notification, PQfreemem));
  }

  return results;
}

} // namespace postgres
} // namespace remote_build
