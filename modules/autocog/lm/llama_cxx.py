
import math
import string

from typing import Any, Dict, List, Tuple, Union, Optional, Callable
from .lm import LM

from ..fta.actions import Choose, Text, Complete

from ..llama import create as autocog_llama_create
from ..llama import tokenize as autocog_llama_tokenize
from ..llama import detokenize as autocog_llama_detokenize
from ..llama import vocab_size as autocog_llama_vocab_size
from ..llama import evaluate as autocog_llama_evaluate

fta_defaults = {
  "Text" : {
    "evaluate" : True
  },
  "Choose" : {
    "threshold" : 0.4,
    "width" : 2
  },
  "Complete" : {
    "threshold" : 0.3,
    "width" : 2,
    "beams" : 3,
    "ahead" : 1,
    "diversity" : 1.,
    "repetition" : .5
  }
}

def fta_to_cxx(model, fta, defaults, safe_mask, char_mask):
    actions = []
    for act in fta.actions.values():
        action = { "uid" : act.uid, "successors" : act.successors }
        if isinstance(act, Text):
            action.update({
              "__type__"  : "Text",
              "evaluate"  : defaults["Text"]["evaluate"] if act.evaluate is None else act.evaluate,
              "tokens"    : autocog_llama_tokenize(model, act.text, False, False)
            })
        elif isinstance(act, Choose):
            if len(act.successors) == 1:
                action.update({ "successors" : [ act.successors[0] ] * len(act.choices) })
            assert len(act.successors) == 0 or len(action['successors']) == len(act.choices)
            action.update({
              "__type__"  : "Choose",
              "choices"   : [
                autocog_llama_tokenize(model, choice[0], False, False) for choice in act.choices
              ],
              "threshold" : defaults["Choose"]["threshold"] if act.threshold is None else act.threshold,
              "width"     : defaults["Choose"]["width"] if act.width is None else act.width
            })
        elif isinstance(act, Complete):
            assert act.length is not None
            action.update({
              "__type__"  : "Complete",
              "length"    : act.length,
              "stop"      : autocog_llama_tokenize(model, act.stop, False, False),
              "vocab"     : safe_mask,
              "threshold" : defaults["Complete"]["threshold"] if act.threshold is None else act.threshold,
              "beams"     : defaults["Complete"]["beams"] if act.beams is None else act.beams,
              "ahead"     : defaults["Complete"]["ahead"] if act.ahead is None else act.ahead,
              "width"     : defaults["Complete"]["width"] if act.width is None else act.width,
            })
        else:
            raise Exception()
        actions.append( action )
    return { 'actions' : actions }

def cxx_to_ftt(model, ftt):
    return {
      "action" : ftt["action"],
      "text" : autocog_llama_detokenize(model, ftt["tokens"], False, False),
      "length" : ftt["length"],
      "logprobs" : ftt["logprobs"],
      "probability" : math.exp(-ftt["logprob"]/ftt["length"]),
      "locproba" : math.exp(-sum(ftt["logprobs"])/len(ftt["logprobs"])),
      "children" : [ cxx_to_ftt(model, child) for child in ftt["children"] ],
      "pruned" : ftt["pruned"],
    }

def create_safe_vocab_mask(model):
    vocab_size = autocog_llama_vocab_size(model)
    mask = [True] * vocab_size
    
    problematic_count = 0
    
    for token_id in range(vocab_size):
        try:
            # Test detokenize single token
            text = autocog_llama_detokenize(model, [token_id], False, False)
            
            # Flag problematic tokens
            is_problematic = (
                '\n' in text or          # Newlines
                '\r' in text or          # Carriage returns  
                '\t' in text or          # Tabs
                len(text) == 0 or        # Empty tokens
                any(ord(c) < 32 for c in text if c not in [' ']) or  # Control characters
                any(ord(c) > 126 for c in text)  # Non-ASCII
            )
            
            if is_problematic:
                mask[token_id] = False
                problematic_count += 1
                
        except Exception:
            # If detokenization fails, exclude the token
            mask[token_id] = False
            problematic_count += 1
    
#    print(f"Excluded {problematic_count}/{vocab_size} problematic tokens")
    return mask
    
def create_single_char_vocab(model):
    vocab_size = autocog_llama_vocab_size(model)
    mask = [False] * vocab_size
    
    # Define character sets to include
    chars_to_test = (
        string.ascii_letters +      # a-z, A-Z
        string.digits +             # 0-9
        string.punctuation +        # .,!? etc.
        ' ' + '\n'                  # spaces
    )
    
    single_char_count = 0
    
    for char in chars_to_test:
        try:
            # Tokenize the single character
            tokens = autocog_llama_tokenize(model, char, False, False)
            
            # Check if it tokenizes to exactly one token
            if len(tokens) == 1:
                token_id = tokens[0]
                if 0 <= token_id < vocab_size:  # Validate range
                    mask[token_id] = True
                    single_char_count += 1
                    
        except Exception:
            # Skip characters that can't be tokenized
            pass
    
#    print(f"Found {single_char_count} single-character tokens from {len(chars_to_test)} tested characters")
    return mask

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
        model = autocog_llama_create(model_path, n_ctx) if len(model_path) > 0 else 0
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
        ftt = autocog_llama_evaluate(self.model, fta)
        ftt = cxx_to_ftt(self.model, ftt)
        paths = extract_paths_from_ftt(ftt)
        return (ftt, paths)

    def impl_greedy(self, **kwargs):
        raise Exception("LlamaCXX does not implement `impl_greedy`")

