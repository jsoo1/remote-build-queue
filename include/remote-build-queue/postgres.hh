#include <map>
#include <string>
#include <variant>

#include <postgres.hh>
#include <uuid.hh>

using std::map;
using std::monostate;
using std::string;
using std::variant;

using remote_build::uuid::Uuid;

namespace remote_build {
namespace queue {

struct EnvVar {
  string val;
};

variant<string, EnvVar> env_conn_param(string const key,
                                       map<string, string> const &env);

variant<string, postgres::ConnectionParams>
env_conn_params(map<string, string> const &env);

variant<string, monostate> no_machine_available(PGconn *, Uuid const &job);

variant<string, monostate> accept_job(PGconn *, Uuid const &job,
                                      string const &store_uri);

} // namespace queue
} // namespace remote_build
