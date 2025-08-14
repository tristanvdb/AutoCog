#ifndef __AUTOCOG_LLAMA_MANAGER__HXX_
#define __AUTOCOG_LLAMA_MANAGER__HXX_

#include "autocog/llama/types.hxx"
#include "autocog/llama/model.hxx"
#include "autocog/llama/evaluation.hxx"

#include <unordered_map>
#include <string>

namespace autocog {
namespace llama {

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

} }

#endif /* __AUTOCOG_LLAMA_MANAGER__HXX_ */

