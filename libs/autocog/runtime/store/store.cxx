
#include "autocog/runtime/store/store.hxx"

namespace autocog::runtime::store {

Registry<sta::Program> & programs() {
    static Registry<sta::Program> instance;
    return instance;
}

Registry<sta::Syntax> & syntaxes() {
    static Registry<sta::Syntax> instance;
    return instance;
}

Registry<sta::SearchConfig> & search_configs() {
    static Registry<sta::SearchConfig> instance;
    return instance;
}

Registry<nlohmann::json> & ftas() {
    static Registry<nlohmann::json> instance;
    return instance;
}

Registry<nlohmann::json> & diagnostics() {
    static Registry<nlohmann::json> instance;
    return instance;
}

}
