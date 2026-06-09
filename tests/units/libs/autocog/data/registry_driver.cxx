// Unit test for autocog::data::Registry<T> and the datastore() singleton.
// Uses a self-contained Fake artifact so the test exercises the registry's
// content-addressing and holder-count semantics without depending on the
// field layout of any real artifact type. Returns non-zero if any check fails.

#include "autocog/data/base.hxx"
#include "autocog/data/registry.hxx"
#include "autocog/data/store.hxx"

#include "autocog/utilities/exception.hxx"

#include <iostream>
#include <memory>
#include <string>

namespace {

int failures = 0;
void check(bool ok, std::string const & what) {
  if (ok) std::cout << "ok   : " << what << "\n";
  else  { std::cerr << "FAIL : " << what << "\n"; ++failures; }
}

// Minimal stored artifact: derives the CRTP Base and supplies a content hash.
struct Fake : autocog::data::Base<Fake> {
  static constexpr char const * format = "fake";
  int value = 0;
  std::string content_hash() const { return std::to_string(value); }
};

std::unique_ptr<Fake> make_fake(int v) {
  auto f = std::make_unique<Fake>();
  f->value = v;
  return f;
}

}

int main() {
  using autocog::data::Registry;
  using autocog::utilities::InternalError;

  Registry<Fake> reg;

  std::string const h1 = reg.add(make_fake(42));
  check(!h1.empty(), "add finalizes and returns a non-empty hash");
  check(reg.contains(h1), "contains(hash) is true after add");
  check(reg.get(h1).value == 42, "get returns the stored artifact");

  std::string const h1b = reg.add(make_fake(42));
  check(h1b == h1, "identical content collapses onto the same hash");

  std::string const h2 = reg.add(make_fake(7));
  check(h2 != h1, "different content yields a different hash");

  // h1 has two holders: the first release keeps it, the second frees it.
  reg.release(h1);
  check(reg.contains(h1), "entry survives while a holder remains");
  reg.release(h1);
  check(!reg.contains(h1), "entry is freed after the last holder releases");

  bool threw = false;
  try { reg.get(h1); } catch (InternalError const &) { threw = true; }
  check(threw, "get on an absent hash throws InternalError");

  reg.release("no-such-hash");          // must not throw
  check(true, "release on an unknown hash is a no-op");
  reg.release(h1);                       // already freed: also a no-op
  check(true, "release on an already-freed hash is a no-op");

  check(reg.contains(h2), "an unrelated entry is unaffected by releases");

  check(&autocog::data::datastore() == &autocog::data::datastore(),
        "datastore() returns a stable singleton");

  std::cout << (failures ? "FAILURES\n" : "all registry checks passed\n");
  return failures ? 1 : 0;
}
