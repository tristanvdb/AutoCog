#include "autocog/data/utility.hxx"

#include <ctime>

#include "picosha2.h"

namespace autocog::data {

std::string utc_timestamp() {
  std::time_t const now = std::time(nullptr);
  std::tm tm{};
  gmtime_r(&now, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return std::string(buf);
}

std::string digest(std::string const & input) {
  std::string hex;
  picosha2::hash256_hex_string(input, hex);
  return hex;
}

}
