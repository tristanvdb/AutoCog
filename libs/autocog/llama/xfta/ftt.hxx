#ifndef AUTOCOG_LLAMA_XFTA_FTT_HXX
#define AUTOCOG_LLAMA_XFTA_FTT_HXX

#include "autocog/llama/xfta/types.hxx"

#include <list>

namespace autocog::llama::xfta {

class FTT {
  public:
    ActionID const action;        //< Action evaluated for this node
    TokenSequence const tokens;   //< Tokens generated at this node
    ProbaSequence const logprobs; //< Logprob for each token

    float const logprob;          //< Cumulative logprob from the root
    unsigned const length;        //< Total length from the root

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

    std::list<FTT> const & get_children() const;
};

}

#endif /* AUTOCOG_LLAMA_XFTA_FTT_HXX */

