
#include "autocog/llama/ftt.hxx"

#if VERBOSE
#  include <iostream>
#endif

#define DEBUG_FTT_add VERBOSE && 0

namespace autocog { namespace llama {

FTT::FTT(
  ActionID const action_,
  TokenSequence const & tokens_,
  ProbaSequence const & logprobs_,
  float logprob_,
  unsigned length_
) :
  action(action_),
  tokens(tokens_),
  logprobs(logprobs_),
  logprob(logprob_),
  length(length_),
  pruned(false),
  children()
{}

FTT & FTT::add(
  ActionID const action_,
  TokenSequence const & tokens_,
  ProbaSequence const & logprobs_
) {
#if DEBUG_FTT_add
  std::cerr << ">> FTT::add <<" << std::endl;
#endif
  float logprob_ = this->logprob;
  for (auto lpb: logprobs_) logprob_ += lpb;
  this->children.emplace_back(action_, tokens_, logprobs_, logprob_, this->length + tokens_.size());
  return this->children.back();
}

FTT * FTT::make_root(TokenSequence const & tokens) {
  ProbaSequence logprobs(tokens.size(), 0.);
  return new FTT(0, tokens, logprobs, 0, tokens.size());
}

// TODO review code below

pybind11::dict FTT::pydict() const {
    pybind11::dict result;
    result["action"] = this->action;
    
    // Convert tokens
    pybind11::list token_list;
    for (TokenID token : this->tokens) {
        token_list.append(token);
    }
    result["tokens"] = token_list;

    // Convert probabilities  
    pybind11::list logprobs_list;
    for (float lpb : this->logprobs) {
        logprobs_list.append(lpb);
    }
    result["logprobs"] = logprobs_list;
    result["logprob"] = this->logprob;
    result["length"] = this->length;

    // Convert children recursively
    pybind11::list children_list;
    for (const FTT& child : this->children) {
        children_list.append(child.pydict());
    }
    result["children"] = children_list;

    // Add metadata
    result["pruned"] = this->pruned;
    
    return result;
}

float FTT::proba() const {
  return std::exp(-this->logprob / this->length);
}

} }

