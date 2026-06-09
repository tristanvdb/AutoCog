#ifndef AUTOCOG_BACKEND_LLAMA_MANAGER_HXX
#define AUTOCOG_BACKEND_LLAMA_MANAGER_HXX

#include "autocog/backend/llama/types.hxx"
#include "autocog/backend/llama/model.hxx"
#include "autocog/backend/llama/evaluation.hxx"

#include <unordered_map>
#include <optional>
#include <string>

namespace autocog::backend::llama {

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

    static EvalID add_eval(ModelID const model_, data::FTA const & fta);
    static Evaluation & get_eval(EvalID id);
    static unsigned advance(EvalID id, std::optional<unsigned> max_token_eval=std::nullopt);
    static data::FTT const & retrieve(EvalID id);
    static void rm_eval(EvalID id);
};

}

#endif // AUTOCOG_BACKEND_LLAMA_MANAGER_HXX
