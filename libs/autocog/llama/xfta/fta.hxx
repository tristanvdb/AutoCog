#ifndef AUTOCOG_LLAMA_XFTA_FTA_HXX
#define AUTOCOG_LLAMA_XFTA_FTA_HXX

#include "autocog/llama/xfta/types.hxx"

#include <optional>
#include <string>
#include <utility>
#include <memory>
#include <stdexcept>

namespace autocog::llama::xfta {

class Model;

struct Vocab {
  std::vector<bool> mask;

  void topk(unsigned k, std::vector<float> const & input, std::vector<std::pair<TokenID, float>> const & output) const;
};

enum class ActionKind {
  Text,
  Completion,
  Choice
};

struct Action {
  ActionKind const kind;
  ActionID const id;
  std::string const name;

  std::vector<ActionID> successors;
    
  Action(ActionKind const kind_, ActionID const id_, std::string const & name_);

  template <class T>
  T const & as() const {
    if (T::Kind != kind) {
      throw std::runtime_error("Calling Action::as() with uncompatible ActionKind.");
    }
    return static_cast<const T &>(*this);
  }
};

struct Text : public Action {
  static constexpr ActionKind Kind = ActionKind::Text;

  bool const evaluate; //< whether to evaluate the probability using the model (else p=1.)

  TokenSequence tokens;

  Text(ActionID const id_, std::string const & name_, bool const eval);
};

struct Completion : public Action {
  static constexpr ActionKind Kind = ActionKind::Completion;

  float const threshold; //< Probability threshold for pruning
  unsigned const length; //< Maximum length of the completion
  unsigned const beams;  //< Number of concurrent exploration beams
  unsigned const ahead;  //< Look ahead parameter for beam search
  unsigned const width;  //< Maximum number of beams to select
  
  std::optional<float> const repetition; //< Penalize repeting pattern
  std::optional<float> const diversity;  //< Encourage diversity across beams

  Vocab vocab;
  TokenSequence stop;

  Completion(
    ActionID const id_, std::string const & name_,
    float threshold_, unsigned length_,
    unsigned beams_, unsigned ahead_, unsigned width_,
    std::optional<float> repetition_,
    std::optional<float> diversity_
  );
};

struct Choice : public Action {
  static constexpr ActionKind Kind = ActionKind::Choice;

  float const threshold; //< Probability threshold for pruning
  unsigned const width;  // Maximum number of choices to select

  std::vector<TokenSequence> choices;  // Each choice is a token sequence

  Choice(ActionID const id_, std::string const & name_, float threshold_, unsigned width_);
};

struct FTA {
  Action const & action(ActionID const id) const;
  std::vector<std::unique_ptr<Action>> actions;
};

}

#endif /* AUTOCOG_LLAMA_XFTA_FTA_HXX */

