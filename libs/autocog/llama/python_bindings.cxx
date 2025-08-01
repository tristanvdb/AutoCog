
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>

#include "fta.hxx"
#include "ftt.hxx"
#include "evaluator.hxx"
#include "tokenizer.hxx"

// Wrapper functions for Python integration
pybind11::dict evaluate_fta_python(uintptr_t ctx_ptr, const pybind11::dict& fta_dict, const pybind11::dict& config_dict = pybind11::dict()) {
    autocog::llama::FTA fta(fta_dict);
    autocog::llama::FTT ftt;

    // TODO: Implementation

    return ftt.pydict();
}

// Tokenizer wrapper functions
std::vector<int> tokenize_python(uintptr_t model_ptr, const std::string& text, bool add_special = true) {
    // TODO: Implementation
    return {};
}

std::string detokenize_python(uintptr_t model_ptr, const std::vector<int>& tokens) {
    // TODO: Implementation
    return "";
}

PYBIND11_MODULE(llama, m) {
  m.doc() = "AutoCog llama.cpp integration module";

  m.def("evaluate_fta", &evaluate_fta_python,
        "Evaluate Finite Thoughts Automata",
        pybind11::arg("ctx_ptr"), pybind11::arg("fta_dict"), pybind11::arg("config_dict") = pybind11::dict());

  m.def("tokenize", &tokenize_python,
        "Tokenize text using llama.cpp tokenizer",
        pybind11::arg("model_ptr"), pybind11::arg("text"), pybind11::arg("add_special") = true);
    
  m.def("detokenize", &detokenize_python,
        "Detokenize tokens using llama.cpp tokenizer",
        pybind11::arg("model_ptr"), pybind11::arg("tokens"));

  pybind11::enum_<autocog::llama::ActionKind>(m, "ActionKind")
      .value("Text",       autocog::llama::ActionKind::Text       )
      .value("Completion", autocog::llama::ActionKind::Completion )
      .value("Choice",     autocog::llama::ActionKind::Choice     );
}

