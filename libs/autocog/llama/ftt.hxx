#ifndef __AUTOCOG_LLAMA_FTT__HXX_
#define __AUTOCOG_LLAMA_FTT__HXX_

#include "autocog/llama/types.hxx"

#include <pybind11/pybind11.h>

#include <list>

namespace autocog {
namespace llama {

class FTT {
  public:
    ActionID const action;
    TokenSequence const tokens;
    ProbaSequence const logprobs;
    float const logprob;
    unsigned const length;

    bool pruned{false};
  private:
    std::list<FTT> children;
    
  public:
    /// I'd like that constructor to be private but it prevents the use of `emplace_back` in `add`.
    /// Adding `friend class std::list<FTT>;` does not solve the issue...
    FTT(ActionID const action_, TokenSequence const & tokens_, ProbaSequence const & logprobs_, float logprob_, unsigned length_);

  public:
    static FTT * make_root(TokenSequence const & tokens_);
    FTT & add(ActionID const action_, TokenSequence const & tokens_, ProbaSequence const & logprobs_);

    float proba() const;

    pybind11::dict pydict() const;
};

} }

#endif /* __AUTOCOG_LLAMA_FTT__HXX_ */

