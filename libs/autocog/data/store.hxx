#ifndef AUTOCOG_DATA_STORE_HXX
#define AUTOCOG_DATA_STORE_HXX

#include "autocog/data/registry.hxx"

#include "autocog/data/syntax.hxx"
#include "autocog/data/search.hxx"
#include "autocog/data/sta.hxx"
#include "autocog/data/fta.hxx"
#include "autocog/data/ftt.hxx"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace autocog::data {

/// The process-wide artifact pool behind the Python bindings: one Registry per
/// format. There is a single shared instance (datastore()); it is not
/// instantiated per binding module.
class DataStore {
  public:
    Registry<Syntax>       syntax;
    Registry<SearchConfig> search;
    Registry<STA>          sta;
    Registry<FTA>          fta;
    Registry<FTT>          ftt;

};

/// The single shared instance used by every binding module.
DataStore & datastore();

}

#endif // AUTOCOG_DATA_STORE_HXX
