#ifndef AUTOCOG_DATA_FTT_HXX
#define AUTOCOG_DATA_FTT_HXX

#include "autocog/data/base.hxx"

#include <cstdint>
#include <list>
#include <optional>
#include <string>
#include <vector>

namespace autocog::data {

using ActionID = unsigned;
using TokenID  = std::int32_t;

/// One node of a finite thought tree. Mirrors the self-contained document the
/// backend produces and walk_ftt_to_frame consumes: raw generation data plus
/// the FTA/model-derived enrichment (uid/field/indices/text) needed to map the
/// tree onto the schema frame without the model or FTA on hand. Conversion
/// lives in the free functions in json.hxx / python.hxx.
struct FTTNode {
  ActionID action = 0;                       ///< FTA action evaluated at this node.
  std::optional<std::string> uid;            ///< Action name (when action is in range).
  std::optional<int> field;                  ///< Schema field index (value nodes only).
  std::optional<std::vector<int>> indices;   ///< Nested field indices, when present.
  std::string text;                          ///< Detokenized text ("" when no tokens).
  float logprob = 0.0f;                      ///< Cumulative logprob from the root.
  std::vector<float> logprobs;               ///< Per-token logprobs at this node.
  unsigned length = 0;                       ///< Total length from the root.
  bool pruned = false;
  std::vector<TokenID> tokens;               ///< Tokens generated at this node.
  std::list<FTTNode> children;

  /// Recursive content hash: digest(fields + child count + each child's hash).
  std::string hash() const;
};

/// A finite thought tree: one artifact identity for the whole tree.
class FTT : public Base<FTT> {
  public:
    static constexpr char const * format = "ftt";

    FTTNode root;

  public:
    std::string content_hash() const;
};

}

#endif // AUTOCOG_DATA_FTT_HXX
