#include "autocog/data/store.hxx"

namespace autocog::data {

DataStore & datastore() {
  static DataStore instance;  // thread-safe initialization (C++11+)
  return instance;
}

}
