#ifndef __AUTOCOG_LLAMA_FTA__HXX_
#define __AUTOCOG_LLAMA_FTA__HXX_

#include <vector>

namespace autocog {
namespace llama {

using NodeID = unsigned;

enum class ActionKind {
  Text,
  Completion,
  Choice
};

class Action {
  public:
    ActionKind const kind;
    NodeID const id;

    Action(ActionKind const kind_, NodeID const id_);

  protected:
    std::vector<NodeID> next;
};

class Text : public Action {
  public:
    Text(NodeID const id_);
};

class Completion : public Action {
  public:
    Completion(NodeID const id_);
};

class Choice : public Action {
  public:
    Choice(NodeID const id_);
};

class FTA {
  public:
    Action const & action(NodeID const & id) const;
  private:
    std::vector<Action> nodes;
};

} }

#endif /* __AUTOCOG_LLAMA_FTA__HXX_ */

