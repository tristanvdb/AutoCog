#ifndef __AUTOCOG_LLAMA_FTA__HXX_
#define __AUTOCOG_LLAMA_FTA__HXX_

#include "autocog/llama/types.hxx"

#include <pybind11/pybind11.h>

#include <utility>

namespace autocog {
namespace llama {

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

  float const threshold; //< Probability threshold for pruning

  std::vector<ActionID> successors;
    
  Action(ActionKind const kind_, ActionID const id_, float threshold_);

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

  TokenSequence tokens;

  Text(ActionID const id_, float threshold_);
};

struct Completion : public Action {
  static constexpr ActionKind Kind = ActionKind::Completion;

  unsigned const length;
  unsigned const beams;
  unsigned const ahead;

  Vocab vocab;
  TokenSequence stop;

  Completion(ActionID const id_, float threshold_, unsigned length_, unsigned beams_, unsigned ahead_);
};

struct Choice : public Action {
  static constexpr ActionKind Kind = ActionKind::Choice;

  unsigned const width;  // Maximum number of choices to explore

  std::vector<TokenSequence> choices;  // Each choice is a token sequence

  Choice(ActionID const id_, float threshold_, unsigned width_);
};

class FTA {
  public:
    Action const & action(ActionID const id) const;

    Text & insert(float threshold_);
    Completion & insert(float threshold_, unsigned length_, unsigned beams_, unsigned ahead_);
    Choice & insert(float threshold_, unsigned width_);

    FTA() = default;
    FTA(Model const & model, pybind11::dict const & pydata);

  private:
    std::vector<std::unique_ptr<Action>> actions;
};

} }

#endif /* __AUTOCOG_LLAMA_FTA__HXX_ */

