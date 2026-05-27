
#include "autocog/runtime/sta/parse.hxx"

namespace autocog::runtime::sta {

using json = nlohmann::json;

json field_value_to_json(FieldValue const & val) {
    if (val.is_string()) {
        return val.as_string();
    } else if (val.is_record()) {
        return field_record_to_json(val.as_record());
    } else if (val.is_array()) {
        json arr = json::array();
        for (auto const & item : val.as_array()) {
            arr.push_back(field_value_to_json(item));
        }
        return arr;
    }
    return nullptr;
}

json field_record_to_json(FieldRecord const & rec) {
    json obj = json::object();
    for (auto const & [key, val] : rec) {
        obj[key] = field_value_to_json(val);
    }
    return obj;
}

}
