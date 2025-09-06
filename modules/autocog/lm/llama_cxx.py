
from typing import Any, Dict, List, Tuple, Union, Optional, Callable
from .lm import LM

from ..llama.xfta import create as xfta_create
from ..llama.xfta import tokenize as xfta_tokenize
from ..llama.xfta import detokenize as xfta_detokenize
from ..llama.xfta import vocab_size as xfta_vocab_size
from ..llama.xfta import evaluate as xfta_evaluate

from ..llama.xfta import fta_defaults, fta_to_cxx, cxx_to_ftt
from ..llama.xfta import create_safe_vocab_mask, create_single_char_vocab

def extract_paths_from_ftt(ftt, current_path=""):
    paths = []
    new_path = current_path + ftt["text"]

    if not "children" in ftt or len(ftt["children"]) == 0:
    	if not ftt["pruned"]:
            paths.append((new_path, ftt["probability"]))
    else:
        for child in ftt["children"]:
            child_paths = extract_paths_from_ftt(child, new_path)
            paths.extend(child_paths)
    return sorted(paths, key=lambda path: path[1], reverse=True)

class LlamaCXX(LM):
    model: Any
    safe_mask: Any
    char_mask: Any

    def __init__(self, model_path:str, n_ctx=4096, **kwargs):
        model = xfta_create(model_path, n_ctx) if len(model_path) > 0 else 0
        super().__init__(
            model=model,
            safe_mask=create_safe_vocab_mask(model),
            char_mask=create_single_char_vocab(model)
        )

    def tokenize(self, text:str, whole:bool=True) -> List[int]:
        raise NotImplementedError("LlamaCXX.tokenize")

    def detokenize(self, tokens:List[int], whole:bool=True) -> str:
        raise NotImplementedError("LlamaCXX.detokenize")

    def evaluate(self, fta):
        fta = fta_to_cxx(self.model, fta, fta_defaults, self.safe_mask, self.char_mask)
        ftt = xfta_evaluate(self.model, fta)
        ftt = cxx_to_ftt(self.model, ftt)
        paths = extract_paths_from_ftt(ftt)
        return (ftt, paths)

    def impl_greedy(self, **kwargs):
        raise Exception("LlamaCXX does not implement `impl_greedy`")

