#include <enqueue/postgres.hh>

namespace remote_build {
namespace enqueue {
namespace postgres {

variant<string, Uuid> enqueue_job(PGconn *conn, BuildRequirements const &reqs) {
  auto drv_path = string(reqs.drv_path.to_string());

  auto needed_system = string(reqs.needed_system.data());

  auto required_features =
      remote_build::postgres::to_sql_array(reqs.required_features);

  char *params[3] = {drv_path.data(), needed_system.data(),
                     required_features.data()};

  auto enqueue_res = remote_build::postgres::exec_params(
      conn,
      "SELECT @schema@.enqueue_job($1::@schema@.drv_filename, "
      "$2::@schema@.textword, $3::@schema@.textword[])",
      3, params);

  if (PQresultStatus(enqueue_res.get()) != PGRES_TUPLES_OK)
    return variant<string, Uuid>(
        remote_build::postgres::err_msg(enqueue_res.get(), "enqueueing job"));

  int n_results;

  int n_fields;

  if ((n_results = PQntuples(enqueue_res.get())) != 1 &&
      (n_fields = PQnfields(enqueue_res.get())) != 1)
    return variant<string, Uuid>(
        nix::fmt("enqueuing returned unexpected number of results: %d rows, %d "
                 "fields (expected 1 row and 1 field)",
                 n_results, n_fields));

  if (PQgetisnull(enqueue_res.get(), 0, 0) == 1)
    return variant<string, Uuid>("enqueueing returned unexpected null");

  Uuid job(string(PQgetvalue(enqueue_res.get(), 0, 0)));

  return variant<string, Uuid>(Uuid(job));
}

variant<string, monostate> cancel_job(PGconn *conn, Uuid const &job) {
  auto escaped_id = remote_build::postgres::escape_uuid(job);

  char *params[1] = {escaped_id.data()};

  auto res = remote_build::postgres::exec_params(
      conn, "SELECT @schema@.cancel_job($1::uuid)", 1, params);

  if (PQresultStatus(res.get()) != PGRES_TUPLES_OK)
    return variant<string, monostate>(
        remote_build::postgres::err_msg(res.get(), "canceling job"));

  return variant<string, monostate>(monostate());
}

variant<string, monostate>
add_inputs_and_outputs(PGconn *conn, Uuid const &job,
                       nix::StorePathSet const &inputs,
                       nix::StringSet const &wanted_outputs) {
  auto escaped_id = remote_build::postgres::escape_uuid(job);

  auto inputs_arr = remote_build::postgres::to_sql_array(inputs);

  auto outputs_arr = remote_build::postgres::to_sql_array(wanted_outputs);

  char *params[3] = {escaped_id.data(), inputs_arr.data(), outputs_arr.data()};

  auto res = remote_build::postgres::exec_params(
      conn,
      "SELECT @schema@.add_inputs_and_outputs("
      "$1::uuid, $2::@schema@.output_path[], $3::@schema@.textword[])",
      3, params);

  if (PQresultStatus(res.get()) != PGRES_TUPLES_OK)
    return variant<string, monostate>(remote_build::postgres::err_msg(
        res.get(), "adding inputs and outputs"));

  return variant<string, monostate>(monostate());
}

} // namespace postgres
} // namespace enqueue
} // namespace remote_build
