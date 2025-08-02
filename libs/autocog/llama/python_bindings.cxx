
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>

#include "fta.hxx"
#include "ftt.hxx"
#include "model.hxx"
#include "evaluator.hxx"

// Wrapper functions for Python integration
pybind11::dict evaluate_fta_python(uintptr_t ptr, const pybind11::dict& fta_dict) {
    auto model = reinterpret_cast<autocog::llama::Model*>(ptr);
    autocog::llama::FTA fta(model, fta_dict);
    autocog::llama::FTT ftt;

    // TODO: Implementation

    return ftt.pydict();
}

PYBIND11_MODULE(llama, module) {
  module.doc() = "AutoCog's llama.cpp integration module";

  module.def("create", [](const std::string& model_path, int n_ctx=4096) {
      auto model = new autocog::llama::Model(model_path, n_ctx);
      return reinterpret_cast<uintptr_t>(model);
  });
    
  module.def("tokenize", [](uintptr_t ptr, const std::string & text, bool add_bos = false, bool special = false) {
      auto model = reinterpret_cast<autocog::llama::Model*>(ptr);
      auto tokens = model->tokenize(text, add_bos, special);
      pybind11::list result;
      for (auto token : tokens) result.append(token);
      return result;
  });
    
  module.def("detokenize", [](uintptr_t ptr, const pybind11::list & py_tokens, bool spec_rm = false, bool spec_unp = false) {
      auto model = reinterpret_cast<autocog::llama::Model*>(ptr);
      autocog::llama::TokenSequence tokens;
      for (auto item : py_tokens) tokens.push_back(item.cast<autocog::llama::TokenID>());
      return model->detokenize(tokens, spec_rm, spec_unp);
  });
    
  module.def("evaluate", &evaluate_fta_python);
}

