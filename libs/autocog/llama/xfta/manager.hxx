#ifndef AUTOCOG_LLAMA_XFTA_MANAGER_HXX
#define AUTOCOG_LLAMA_XFTA_MANAGER_HXX

#include "autocog/llama/xfta/types.hxx"
#include "autocog/llama/xfta/model.hxx"
#include "autocog/llama/xfta/evaluation.hxx"

#include <unordered_map>
#include <string>

namespace autocog::llama::xfta {

class Evaluation;
class FTA;

class Manager {
  public:
    static bool initialized;
  private:
    std::vector<Model> models;
    
    EvalID next_eval_id = 0;
    std::unordered_map<EvalID, Evaluation> evaluations;

    Manager() = default;
    void cleanup();
    static Manager & instance();

  public:
    ~Manager();

    static void initialize();

    static ModelID add_model(std::string const & path, int n_ctx);
    static Model & get_model(ModelID id);

    static EvalID add_eval(ModelID const model_, FTA const & fta);
    static Evaluation & get_eval(EvalID id);
    static void rm_eval(EvalID id);
};

}

#endif /* AUTOCOG_LLAMA_XFTA_MANAGER_HXX */

