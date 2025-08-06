
#include "autocog/llama/manager.hxx"
#include "autocog/llama/evaluation.hxx"

#include <string>
#include <cstdlib>
#include <stdexcept>

namespace autocog {
namespace llama {

Manager & Manager::instance() {
  static Manager __instance;
  return __instance;
}

Manager::~Manager() {
  cleanup();
}

void Manager::cleanup() {
  models.clear();
  llama_backend_free();
}

void Manager::initialize() {
  llama_backend_init();

  auto & manager = instance();
  manager.models.emplace_back(); // adding Model #0 which is a simple character level random number generator (for ultra-fast testing)

  std::atexit([]() { 
    instance().cleanup(); 
  });
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
  manager.evaluations.try_emplace(id, model, fta);
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

