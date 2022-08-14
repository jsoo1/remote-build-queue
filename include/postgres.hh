#pragma once

#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <libpq-fe.h>

#include <nix/store-api.hh>
#include <nix/util.hh>

#include <concat-strings.hh>
#include <uuid.hh>

using nix::overloaded;
using std::monostate;
using std::optional;
using std::set;
using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::variant;
using std::vector;

using remote_build::uuid::Uuid;

namespace remote_build {
namespace postgres {

struct ConnectionParams {
  string const user;
  string const host;
  string const port;
  string const dbname;
};

variant<string, shared_ptr<PGconn>> connect(ConnectionParams const &params);

string show_conn_string(PGconn *conn);

shared_ptr<PGresult> exec(PGconn *conn, string const &stmt);

shared_ptr<PGresult> exec_params(PGconn *conn, string const &stmt, int n_params,
                                 char **params);

variant<string, shared_ptr<char>> escape_identifier(PGconn *conn,
                                                    string const &ident);

string err_msg(PGconn *conn, string const &additional_msg);

string err_msg(PGconn *conn);

string err_msg(PGresult *res, string const &additional_msg);

string err_msg(PGresult *res);

string to_sql_array(vector<string> const &v);

string to_sql_array(set<string> const &v);

string to_sql_array(nix::StorePathSet const &s);

// FIXME: Not a nice escaping mechanism
string escape_uuid(Uuid const &u);

struct PollingConnectionClosed {
  string msg = "postgres connection was closed";
};

struct PollingFailed {
  string err;
  PollingFailed(string err) : err(err) {}
  string msg() { return err; };
};

struct PollingTimedOut {
  string msg = "polling postgres socket timed out unexpectedly";
};

struct PollingUnexpectedNumResults {
  int n;
  PollingUnexpectedNumResults(int n) : n(n) {}
  string msg() {
    std::ostringstream ss;

    ss << "polling postgres socket got unexpected number of results: " << n;
    return ss.str();
  }
};

struct PollingPollErr {
  string msg = "error occured on postgres socket";
};

struct PollingPollHup {
  string msg = "socket hung up while polling postgres";
};

struct PollingPollInval {
  string msg = "invalid socket while polling postgres (socket closed)";
};

typedef variant<PollingConnectionClosed, PollingFailed, PollingTimedOut,
                PollingUnexpectedNumResults, PollingPollErr, PollingPollHup,
                PollingPollInval>
    PollingError;

typedef variant<PollingError, monostate> PollingResult;

string err_msg(PollingError err);

PollingResult poll_socket_ready(PGconn *conn);

vector<shared_ptr<PGnotify>> collect_notifications(PGconn *conn);

struct FromRowError {
  string msg;
  FromRowError(string const &msg) : msg(msg) {}
};

template <class T> using FromRowResult = variant<FromRowError, T>;

template <class T, size_t N>
using FromRow =
    std::function<FromRowResult<T>(std::array<optional<string>, N>)>;

template <class T> using FromRowResults = vector<FromRowResult<T>>;

template <class T, size_t N>
FromRowResults<T> collect_tuples(PGresult *res, FromRow<T, N> f) {
  int n_fields = PQnfields(res);

  // Losslessly widen the int
  // https://stackoverflow.com/a/6963759/17046150
  auto n_fields_u =
      static_cast<std::make_unsigned<decltype(n_fields)>::type>(n_fields);

  if (n_fields_u > N)
    throw std::out_of_range(
        nix::fmt("postgres result is smaller than required size %d, got %d", N,
                 PQnfields(res)));

  FromRowResults<T> results;

  for (int i = 0; i < PQntuples(res); i++) {
    std::array<optional<string>, N> tup;

    for (int j = 0; j < n_fields; j++) {
      tup[j] = (PQgetisnull(res, i, j) == 1)
                   ? std::nullopt
                   : std::make_optional<string>(PQgetvalue(res, i, j));
    }
    results.emplace_back(f(tup));
  }
  return results;
}

} // namespace postgres
} // namespace remote_build
