
#include "autocog/runtime/fta/ftt.hxx"
#include "autocog/logging.hxx"

#include <cmath>



namespace autocog::runtime::fta {

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
  SPDLOG_LOGGER_TRACE(autocog::log(), ">> FTT::add <<");
  float logprob_ = this->logprob;
  for (auto lpb: logprobs_) logprob_ += lpb;
  this->children.emplace_back(action_, tokens_, logprobs_, logprob_, this->length + tokens_.size());
  return this->children.back();
}

FTT * FTT::make_root(TokenSequence const & tokens) {
  ProbaSequence logprobs(tokens.size(), 0.);
  return new FTT(0, tokens, logprobs, 0, tokens.size());
}

float FTT::proba() const {
  return std::exp(-this->logprob / this->length);
}

std::list<FTT> const & FTT::get_children() const {
  return this->children;
}

}

