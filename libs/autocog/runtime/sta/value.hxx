#ifndef AUTOCOG_RUNTIME_STA_VALUE_HXX
#define AUTOCOG_RUNTIME_STA_VALUE_HXX

#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace autocog::runtime::sta {

struct FieldValue;
using FieldRecord = std::map<std::string, FieldValue>;
using FieldArray = std::vector<FieldValue>;

struct FieldValue {
    // Use unique_ptr to break the recursive type for GCC
    std::variant<std::string, std::unique_ptr<FieldRecord>, std::unique_ptr<FieldArray>> data;

    FieldValue() : data(std::string("")) {}
    FieldValue(std::string s) : data(std::move(s)) {}
    FieldValue(FieldRecord r) : data(std::make_unique<FieldRecord>(std::move(r))) {}
    FieldValue(FieldArray a) : data(std::make_unique<FieldArray>(std::move(a))) {}

    FieldValue(FieldValue const & other);
    FieldValue(FieldValue &&) = default;
    FieldValue & operator=(FieldValue const & other);
    FieldValue & operator=(FieldValue &&) = default;

    bool is_string() const { return std::holds_alternative<std::string>(data); }
    bool is_record() const { return std::holds_alternative<std::unique_ptr<FieldRecord>>(data); }
    bool is_array()  const { return std::holds_alternative<std::unique_ptr<FieldArray>>(data); }

    std::string const & as_string() const { return std::get<std::string>(data); }
    FieldRecord const & as_record() const { return *std::get<std::unique_ptr<FieldRecord>>(data); }
    FieldArray const & as_array()   const { return *std::get<std::unique_ptr<FieldArray>>(data); }

    FieldRecord & as_record() { return *std::get<std::unique_ptr<FieldRecord>>(data); }
    FieldArray & as_array()  { return *std::get<std::unique_ptr<FieldArray>>(data); }
};

}

#endif // AUTOCOG_RUNTIME_STA_VALUE_HXX
