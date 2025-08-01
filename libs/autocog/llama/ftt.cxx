
#include "autocog/llama/ftt.hxx"

namespace autocog { namespace llama {

// TODO review code below

pybind11::dict FTT::pydict() const {
    pybind11::dict result;
    
    // Convert tokens
    pybind11::list token_list;
    for (TokenID token : tokens) {
        token_list.append(token);
    }
    result["tokens"] = token_list;

    // Convert probabilities  
    pybind11::list proba_list;
    for (float prob : probas) {
        proba_list.append(prob);
    }
    result["probabilities"] = proba_list;

    // Convert children recursively
    pybind11::list children_list;
    for (const FTT& child : children) {
        children_list.append(child.pydict());
    }
    result["children"] = children_list;

    // Add metadata
    result["pruned"] = pruned;
    
    return result;
}

} }

