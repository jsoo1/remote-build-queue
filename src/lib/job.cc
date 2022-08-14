#include <memory>

#include <nix/path.hh>

#include <job.hh>
#include <postgres.hh>

using std::shared_ptr;

using nlohmann::json;

namespace remote_build {
namespace job {

variant<string, Job> get(PGconn *conn, Uuid const &id) {

  char const *select_job_stmt =
      "SELECT * FROM ROWS FROM (@schema@.get_job($1::uuid))";

  auto escaped = postgres::escape_uuid(id);

  char *params[1] = {escaped.data()};

  auto res = postgres::exec_params(conn, select_job_stmt, 1, params);

  if (PQresultStatus(res.get()) != PGRES_TUPLES_OK)
    return variant<string, Job>(
        postgres::err_msg(res.get(), nix::fmt("getting job %s", id.val)));

  auto job_res = from_postgres(res.get());

  if (std::holds_alternative<string>(job_res))
    return variant<string, Job>(std::get<string>(job_res));

  return variant<string, Job>(std::get<Job>(job_res));
}

variant<string, Job> from_postgres(PGresult *res) {
  int n_results;

  int n_fields;

  if ((n_results = PQntuples(res) != 1) && (n_fields = PQnfields(res) != 3))
    return variant<string, Job>(
        nix::fmt("getting job returned unexpected results: %d rows, %d fields"
                 " (expected 1 row and 3 fields)"));

  if (PQgetisnull(res, 0, 0) == 1 || PQgetisnull(res, 0, 1) == 1 ||
      PQgetisnull(res, 0, 2) == 1)
    return variant<string, Job>("getting job returned unexpected null");

  auto toks = nix::tokenizeString<set<string>>(PQgetvalue(res, 0, 2), "{},");

  return variant<string, Job>(
      Job(PQgetvalue(res, 0, 0), PQgetvalue(res, 0, 1), toks));
}

} // namespace job
} // namespace remote_build
