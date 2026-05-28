#ifndef AUTOCOG_RUNTIME_STORE_HXX
#define AUTOCOG_RUNTIME_STORE_HXX

#include "autocog/runtime/sta/state.hxx"
#include "autocog/runtime/sta/syntax.hxx"
#include "autocog/runtime/sta/search.hxx"

#include <nlohmann/json.hpp>

#include <atomic>
#include <map>
#include <mutex>
#include "autocog/utilities/exception.hxx"

namespace autocog::runtime::store {

template<typename T>
class Registry {
    std::map<int, T> items;
    std::atomic<int> next_id{1};
    std::mutex mutex;

  public:
    int add(T value) {
        std::lock_guard lock(mutex);
        int id = next_id++;
        items.emplace(id, std::move(value));
        return id;
    }

    T const & get(int id) const {
        auto it = items.find(id);
        if (it == items.end()) throw autocog::utilities::InternalError("Store: invalid ID " + std::to_string(id));
        return it->second;
    }

    T & get(int id) {
        auto it = items.find(id);
        if (it == items.end()) throw autocog::utilities::InternalError("Store: invalid ID " + std::to_string(id));
        return it->second;
    }

    void remove(int id) {
        std::lock_guard lock(mutex);
        items.erase(id);
    }

    bool has(int id) const {
        return items.count(id) > 0;
    }
};

// Global registries — shared across all binding modules via the shared library
Registry<sta::Program> & programs();
Registry<sta::Syntax>  & syntaxes();
Registry<sta::SearchConfig> & search_configs();
Registry<nlohmann::json> & ftas();

}

#endif // AUTOCOG_RUNTIME_STORE_HXX
