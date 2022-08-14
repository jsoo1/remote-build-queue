#pragma once

#include <string>

#include <nlohmann/json.hpp>

using nlohmann::json;
using std::string;

namespace remote_build {
namespace uuid {

struct Uuid {
  const string val;
  Uuid(string const &val) : val(string(val)) {}
  Uuid(json const &j) : val(string(j.get<string>())) {}
};

} // namespace uuid
} // namespace remote_build
