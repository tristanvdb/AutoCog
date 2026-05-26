
from .xfta_cxx import create, tokenize, detokenize, vocab_size, evaluate
from .convert import fta_to_cxx, cxx_to_ftt
from .vocabs import create_safe_vocab_mask, create_single_char_vocab

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

