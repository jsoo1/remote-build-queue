#include <sstream>
#include <variant>

#include <remote-build-queue/postgres.hh>

using std::get;
using std::variant;

namespace remote_build {
namespace queue {

variant<string, EnvVar> env_conn_param(string const key,
                                       map<string, string> const &env) {
  auto res = env.find(key);

  if (res == env.end()) {
    std::ostringstream ss;

    ss << "missing $" << key;

    return variant<string, EnvVar>(ss.str());

  } else {
    return variant<string, EnvVar>(EnvVar{
        .val = res->second,
    });
  }
}

variant<string, postgres::ConnectionParams>
env_conn_params(map<string, string> const &env) {
  auto user = env_conn_param("PG_USER", env);

  if (std::holds_alternative<string>(user))
    return variant<string, postgres::ConnectionParams>(std::get<string>(user));

  auto host = env_conn_param("PG_HOST", env);

  if (std::holds_alternative<string>(host))
    return variant<string, postgres::ConnectionParams>(get<string>(host));

  auto port = env_conn_param("PG_PORT", env);

  if (std::holds_alternative<string>(port))
    return variant<string, postgres::ConnectionParams>(get<string>(port));

  auto dbname = env_conn_param("PG_DBNAME", env);

  if (std::holds_alternative<string>(dbname))
    return variant<string, postgres::ConnectionParams>(get<string>(dbname));

  return postgres::ConnectionParams{
      .user = get<EnvVar>(user).val,
      .host = get<EnvVar>(host).val,
      .port = get<EnvVar>(port).val,
      .dbname = get<EnvVar>(dbname).val,
  };
}

variant<string, monostate> no_machine_available(PGconn *conn, Uuid const &job) {
  auto escaped = postgres::escape_uuid(job);

  char *params[1] = {escaped.data()};

  auto res = postgres::exec_params(
      conn,
      "INSERT INTO @schema@.events (name, job) "
      "VALUES ('no-machine-available'::@schema@.event, $1::uuid)",
      1, params);

  if (PQresultStatus(res.get()) != PGRES_COMMAND_OK)
    return variant<string, monostate>(postgres::err_msg(
        res.get(), "inserting 'no-machine-available' " + job.val));

  return variant<string, monostate>(monostate());
}

variant<string, monostate> accept_job(PGconn *conn, Uuid const &job,
                                      string const &store_uri) {
  auto escaped_id = postgres::escape_uuid(job);

  auto uri = string(store_uri);

  char *params[2] = {escaped_id.data(), uri.data()};

  auto res = postgres::exec_params(
      conn, "SELECT @schema@.accept_job($1::uuid, $2::@schema@.textword)", 2,
      params);

  if (PQresultStatus(res.get()) != PGRES_TUPLES_OK)
    return variant<string, monostate>(
        postgres::err_msg(res.get(), "accepting job " + job.val));

  return variant<string, monostate>(monostate());
}

} // namespace queue
} // namespace remote_build
