
#include "autocog/parser/stl/parser.hxx"
#include "autocog/parser/stl/ir.hxx"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

// Helper to convert C++ IR to Python dict
py::dict ir_to_dict(const autocog::parser::IrProgram& program) {
    py::dict result;
    
    // Convert variables
    py::list vars;
    for (const auto& var : program.variables) {
        py::dict v;
        v["name"] = var.name;
        
        // Convert value variant
        if (std::holds_alternative<std::string>(var.value)) {
            v["value"] = std::get<std::string>(var.value);
        } else if (std::holds_alternative<int64_t>(var.value)) {
            v["value"] = std::get<int64_t>(var.value);
        } else if (std::holds_alternative<double>(var.value)) {
            v["value"] = std::get<double>(var.value);
        } else if (std::holds_alternative<bool>(var.value)) {
            v["value"] = std::get<bool>(var.value);
        } else {
            v["value"] = py::none();
        }
        
        v["is_argument"] = var.is_argument;
        vars.append(v);
    }
    if (!program.variables.empty()) {
        result["variables"] = vars;
    }
    
    // Convert formats
    py::list formats;
    for (const auto& format : program.formats) {
        py::dict f;
        f["name"] = format.name;
        
        py::list fields;
        for (const auto& field : format.fields) {
            py::dict fld;
            fld["name"] = field.name;
            fld["type"] = static_cast<int>(field.type);
            if (!field.type_name.empty()) {
                fld["type_name"] = field.type_name;
            }
            if (field.default_value.has_value()) {
                const auto& val = field.default_value.value();
                if (std::holds_alternative<std::string>(val)) {
                    fld["default_value"] = std::get<std::string>(val);
                } else if (std::holds_alternative<int64_t>(val)) {
                    fld["default_value"] = std::get<int64_t>(val);
                } else if (std::holds_alternative<double>(val)) {
                    fld["default_value"] = std::get<double>(val);
                } else if (std::holds_alternative<bool>(val)) {
                    fld["default_value"] = std::get<bool>(val);
                }
            }
            if (field.array_size.has_value()) {
                fld["array_size"] = field.array_size.value();
            }
            fld["is_argument"] = field.is_argument;
            if (!field.annotations.empty()) {
                fld["annotations"] = field.annotations;
            }
            fields.append(fld);
        }
        f["fields"] = fields;
        
        if (!format.annotations.empty()) {
            f["annotations"] = format.annotations;
        }
        
        formats.append(f);
    }
    if (!program.formats.empty()) {
        result["formats"] = formats;
    }
    
    // Convert structs
    py::list structs;
    for (const auto& strct : program.structs) {
        py::dict s;
        s["name"] = strct.name;
        
        py::list fields;
        for (const auto& field : strct.fields) {
            py::dict fld;
            fld["name"] = field.name;
            fld["type"] = static_cast<int>(field.type);
            if (!field.type_name.empty()) {
                fld["type_name"] = field.type_name;
            }
            if (field.array_size.has_value()) {
                fld["array_size"] = field.array_size.value();
            }
            fld["is_argument"] = field.is_argument;
            if (!field.annotations.empty()) {
                fld["annotations"] = field.annotations;
            }
            fields.append(fld);
        }
        s["fields"] = fields;
        
        if (!strct.annotations.empty()) {
            s["annotations"] = strct.annotations;
        }
        
        structs.append(s);
    }
    if (!program.structs.empty()) {
        result["structs"] = structs;
    }
    
    // Convert prompts
    py::list prompts;
    for (const auto& prompt : program.prompts) {
        py::dict p;
        p["name"] = prompt.name;
        
        // Fields
        py::list fields;
        for (const auto& field : prompt.fields) {
            py::dict fld;
            fld["name"] = field.name;
            fld["type"] = static_cast<int>(field.type);
            if (!field.type_name.empty()) {
                fld["type_name"] = field.type_name;
            }
            if (field.array_size.has_value()) {
                fld["array_size"] = field.array_size.value();
            }
            fld["is_argument"] = field.is_argument;
            if (!field.annotations.empty()) {
                fld["desc"] = field.annotations;  // Python IR uses 'desc'
            }
            fields.append(fld);
        }
        p["fields"] = fields;
        
        // Channels
        py::list channels;
        for (const auto& channel : prompt.channels) {
            py::dict ch;
            ch["target_path"] = channel.target_path;
            
            if (channel.source_path.has_value()) {
                ch["source_path"] = channel.source_path.value();
            }
            
            if (channel.call.has_value()) {
                py::dict call;
                const auto& c = channel.call.value();
                
                if (c.extern_cog.has_value()) {
                    call["extern"] = c.extern_cog.value();
                }
                if (c.entry_point.has_value()) {
                    call["entry"] = c.entry_point.value();
                }
                if (!c.kwargs.empty()) {
                    call["kwargs"] = c.kwargs;
                }
                if (!c.maps.empty()) {
                    call["maps"] = c.maps;
                }
                if (!c.binds.empty()) {
                    call["binds"] = c.binds;
                }
                
                ch["call"] = call;
            }
            
            channels.append(ch);
        }
        if (!prompt.channels.empty()) {
            p["channels"] = channels;
        }
        
        // Flows
        if (!prompt.flows.empty()) {
            py::dict flows;
            for (const auto& [tag, flow] : prompt.flows) {
                py::dict f;
                f["prompt"] = flow.target_prompt;  // Python IR uses 'prompt' not 'target_prompt'
                f["limit"] = flow.limit;
                if (flow.alias.has_value()) {
                    f["alias"] = flow.alias.value();
                }
                flows[tag.c_str()] = f;
            }
            p["flows"] = flows;
        }
        
        // Return
        if (prompt.return_stmt.has_value()) {
            py::dict ret;
            const auto& r = prompt.return_stmt.value();
            
            if (r.alias.has_value()) {
                ret["alias"] = r.alias.value();
            }
            
            py::list fields;
            for (const auto& [path, rename] : r.fields) {
                py::dict field;
                field["path"] = path;
                if (rename.has_value()) {
                    field["rename"] = rename.value();
                }
                fields.append(field);
            }
            ret["fields"] = fields;
            
            p["return"] = ret;
        }
        
        // Annotations (desc in Python)
        if (!prompt.annotations.empty()) {
            p["desc"] = prompt.annotations;
        }
        
        prompts.append(p);
    }
    if (!program.prompts.empty()) {
        result["prompts"] = prompts;
    }
    
    // Global flows
    if (!program.global_flows.empty()) {
        py::dict flows;
        for (const auto& [tag, flow] : program.global_flows) {
            py::dict f;
            f["prompt"] = flow.target_prompt;
            f["limit"] = flow.limit;
            if (flow.alias.has_value()) {
                f["alias"] = flow.alias.value();
            }
            flows[tag.c_str()] = f;
        }
        result["global_flows"] = flows;
    }
    
    // Global annotations
    if (!program.global_annotations.empty()) {
        result["global_annotations"] = program.global_annotations;
    }
    
    return result;
}

PYBIND11_MODULE(stl_parser_cpp, m) {
    m.doc() = "C++ STL parser for AutoCog";
    
    // FieldType enum
    py::enum_<autocog::parser::FieldType>(m, "FieldType")
        .value("TEXT", autocog::parser::FieldType::TEXT)
        .value("SELECT", autocog::parser::FieldType::SELECT)
        .value("GENERATE", autocog::parser::FieldType::GENERATE)
        .value("INTEGER", autocog::parser::FieldType::INTEGER)
        .value("BOOLEAN", autocog::parser::FieldType::BOOLEAN)
        .value("FLOAT", autocog::parser::FieldType::FLOAT)
        .value("CUSTOM", autocog::parser::FieldType::CUSTOM);
    
    // Diagnostic class
    py::class_<autocog::parser::Diagnostic>(m, "Diagnostic")
        .def_readonly("level", &autocog::parser::Diagnostic::level)
        .def_property_readonly("line", [](const autocog::parser::Diagnostic& d) { return d.location.line; })
        .def_property_readonly("column", [](const autocog::parser::Diagnostic& d) { return d.location.column; })
        .def_readonly("message", &autocog::parser::Diagnostic::message)
        .def_readonly("source_line", &autocog::parser::Diagnostic::source_line)
        .def_readonly("notes", &autocog::parser::Diagnostic::notes)
        .def("format", &autocog::parser::Diagnostic::format)
        .def("__str__", &autocog::parser::Diagnostic::format);
    
    // Diagnostic Level enum
    py::enum_<autocog::parser::Diagnostic::Level>(m, "DiagnosticLevel")
        .value("Error", autocog::parser::Diagnostic::Level::Error)
        .value("Warning", autocog::parser::Diagnostic::Level::Warning)
        .value("Note", autocog::parser::Diagnostic::Level::Note);
    
    // Main parsing functions
    m.def("parse_file", [](const std::string& filename) {
        std::vector<autocog::parser::Diagnostic> diagnostics;
        auto ir = autocog::parser::parse_file(filename, diagnostics);
        
        py::dict result;
        result["success"] = (ir != nullptr);
        result["diagnostics"] = diagnostics;
        
        if (ir) {
            result["ir"] = ir_to_dict(*ir);
            result["json"] = autocog::parser::ir_to_json(*ir);
        }
        
        return result;
    }, py::arg("filename"),
       "Parse an STL file and return IR as Python dict");
    
    m.def("parse_string", [](const std::string& source) {
        std::vector<autocog::parser::Diagnostic> diagnostics;
        auto ir = autocog::parser::parse_string(source, diagnostics);
        
        py::dict result;
        result["success"] = (ir != nullptr);
        result["diagnostics"] = diagnostics;
        
        if (ir) {
            result["ir"] = ir_to_dict(*ir);
            result["json"] = autocog::parser::ir_to_json(*ir);
        }
        
        return result;
    }, py::arg("source"),
       "Parse an STL string and return IR as Python dict");
    
    m.def("validate_ir", [](py::dict ir_dict) {
        // For now, just return true
        // Full implementation would convert dict back to C++ IR and validate
        return py::make_tuple(true, py::list());
    }, py::arg("ir"),
       "Validate IR consistency");
    
    // Version info
    m.attr("__version__") = "1.0.0";
}
