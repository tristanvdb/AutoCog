
#include "autocog/compiler/stl/serialize.hxx"
#include "autocog/compiler/stl/driver.hxx"
#include "helpers.hxx"

namespace autocog::compiler::stl {

void serialize_symbols(Driver const & driver, std::ostream & out) {
    using json = serialize::json;

    json output;
    output["symbols"] = json::object();
    for (auto const & [qname, sym] : driver.tables.symbols) {
        json entry;
        std::visit([&](auto const & s) {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, DefineSymbol>) {
                switch (s.node.data.kind) {
                    case ast::DefineKind::Define:   entry["type"] = "define"; break;
                    case ast::DefineKind::Argument: entry["type"] = "argument"; break;
                    case ast::DefineKind::Vocab:    entry["type"] = "vocab"; break;
                }
            } else if constexpr (std::is_same_v<T, RecordSymbol>) {
                entry["type"] = "record";
            } else if constexpr (std::is_same_v<T, PromptSymbol>) {
                entry["type"] = "prompt";
            } else if constexpr (std::is_same_v<T, PythonSymbol>) {
                entry["type"] = "python";
                entry["filename"] = s.filename;
            } else if constexpr (std::is_same_v<T, UnresolvedImport>) {
                entry["type"] = "import";
                entry["fileid"] = s.fileid;
            } else if constexpr (std::is_same_v<T, UnresolvedAlias>) {
                entry["type"] = "alias";
                entry["fileid"] = s.fileid;
            }
        }, sym);
        output["symbols"][qname] = entry;
    }

    output["files"] = json::object();
    for (auto const & [name, fid] : driver.fileids) {
        output["files"][name] = fid;
    }

    out << output.dump(2) << std::endl;
}

}
