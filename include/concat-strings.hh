#pragma once

#include <set>
#include <sstream>
#include <string>
#include <vector>

using std::set;
using std::string;
using std::vector;

namespace remote_build {
namespace concat_strings {

string sep(vector<string> const &els, string const &s);

string sep(set<string> const &els, string const &s);

} // namespace concat_strings
} // namespace remote_build
