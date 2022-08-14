#include <concat-strings.hh>

namespace remote_build {
namespace concat_strings {

string sep(vector<string> const &els, string const &s) {
  std::ostringstream ss;

  auto begin = els.begin();

  auto end = els.end();

  if (begin != end) {
    ss << *begin;

    ++begin;
  }

  while (begin != end) {
    ss << s << *begin;
    ++begin;
  }

  return ss.str();
}

string sep(set<string> const &els, string const &s) {
  std::ostringstream ss;

  auto begin = els.begin();

  auto end = els.end();

  if (begin != end) {
    ss << *begin;

    ++begin;
  }

  while (begin != end) {
    ss << s << *begin;
    ++begin;
  }

  return ss.str();
}

} // namespace concat_strings
} // namespace remote_build
