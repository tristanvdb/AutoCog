
from typing import Any, Dict, List, Tuple, Union, Optional, Callable
from .lm import LM

from ..llama import create as autocog_llama_create

class LlamaCXX(LM):
    model: Any

    def __init__(self, model_path:str, n_ctx=2048, **kwargs):
        super().__init__(
            model=autocog_llama_create(model_path, n_ctx) if len(model_path) > 0 else 0
        )

    def tokenize(self, text:str, whole:bool=True) -> List[int]:
        raise NotImplementedError("LlamaCXX.tokenize")

    def detokenize(self, tokens:List[int], whole:bool=True) -> str:
        raise NotImplementedError("LlamaCXX.detokenize")

    def evaluate(self, sta):
        raise NotImplementedError("LlamaCXX.evaluate")

    def impl_greedy(self, **kwargs):
        raise Exception("LlamaCXX does not implement `impl_greedy`")

