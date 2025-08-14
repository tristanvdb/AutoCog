
#include "autocog/llama/manager.hxx"
#include "autocog/llama/evaluation.hxx"

#include <string>
#include <cstdlib>
#include <stdexcept>

#if VERBOSE
#  include <iostream>
#endif

namespace autocog {
namespace llama {

void quiet_log_callback(enum ggml_log_level level, const char * text, void * user_data) {
    if (level == GGML_LOG_LEVEL_ERROR) {
        fprintf(stderr, "%s", text);
    }
}

bool Manager::initialized{false};

Manager & Manager::instance() {
  static Manager __instance;
  return __instance;
}

Manager::~Manager() {
  cleanup();
}

void Manager::cleanup() {
#if VERBOSE
  std::cerr << "Manager::cleanup()" << std::endl;
#endif
  if (Manager::initialized) {
    evaluations.clear();
    models.clear();
    llama_backend_free();
    Manager::initialized = false;
  }
}

void Manager::initialize() {
#if VERBOSE
  std::cerr << "Manager::initialize()" << std::endl;
#endif
#if VERBOSE == 0
  llama_log_set(quiet_log_callback, nullptr);
#endif
  llama_backend_init();

  auto & manager = instance();
  manager.models.emplace_back(); // adding Model #0 which is a simple character level random number generator (for ultra-fast testing)

  std::atexit([]() { 
    instance().cleanup(); 
  });
  Manager::initialized = true;
}

ModelID Manager::add_model(std::string const & path, int n_ctx) {
  auto & manager = instance();
  ModelID id = manager.models.size();
  manager.models.emplace_back(id, path, n_ctx);
  return id;
}

Model & Manager::get_model(ModelID id) {
  auto & manager = instance();
  return manager.models[id];
}

EvalID Manager::add_eval(ModelID const model, FTA const & fta) {
  auto & manager = instance();
  EvalID id = manager.next_eval_id++;
  EvaluationConfig config;
  manager.evaluations.try_emplace(id, config, model, fta);
  return id;
}

Evaluation & Manager::get_eval(EvalID id) {
  auto & manager = instance();
  auto it = manager.evaluations.find(id);
  if (it == manager.evaluations.end()) {
    throw std::runtime_error("Invalid Evaluation ID: " + std::to_string(id));
  }
  return it->second;
}

void Manager::rm_eval(EvalID id) {
  auto & manager = instance();
  manager.evaluations.erase(id);
}

} }

