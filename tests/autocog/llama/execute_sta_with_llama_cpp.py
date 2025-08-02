
import sys, json

from autocog.sta.compile import compile_source_to_program_and_stas
from autocog.sta.syntax import Syntax
from autocog.sta.runtime import Frame

from autocog.fta.automaton import FiniteThoughtAutomaton as FTA
from autocog.fta.actions import Choose, Text, Complete

import autocog.llama
from autocog.llama import tokenize, detokenize

sta_file = sys.argv[1]
json_data = sys.argv[2]
model_path = sys.argv[3]

sta = compile_source_to_program_and_stas(
  open(sta_file, 'r').read()
)[1]['main']

fta = sta.instantiate(
  syntax=Syntax(),
  frame=Frame(
    state={ st.label() : None for st in sta.concretes.values() if st.abstract.field is not None },
    data=json.loads(json_data)
  ),
  branches={},
  inputs=None
)

model = autocog.llama.create(model_path, 4096)

actions = {
##        "root": {
##            "__type__": "Text",
##            "threshold": 0.0,
##            "tokens": [1, 2, 3],
##            "successors": ["choice1"]
##        },
##        "choice1": {
##            "__type__": "Choice", 
##            "threshold": 0.1,
##            "width": 2,
##            "choices": [[4, 5], [6, 7]],
##            "successors": []
##        }
}

for act in fta.actions.values():
    action = { "successors" : act.successors }
    if isinstance(act, Text):
        action.update({
          "__type__" : "Text",
          "threshold" : 1. if act.threshold is None else act.threshold,
          "tokens" : tokenize(model, act.text, False, False)
        })
    elif isinstance(act, Choose):
        action.update({
          "__type__" : "Text",
          "threshold" : 1. if act.threshold is None else act.threshold,
          "width" : 1 if act.width is None else act.width,
          "choices" : [
            tokenize(model, choice[0], False, False) for choice in act.choices
          ]
    	})
    elif isinstance(act, Complete):
        action.update({
          "__type__" : "Text",
          "threshold" : 1. if act.threshold is None else act.threshold,
          "length" : 1 if act.length is None else act.length,
          "beams" : 1 if act.beams is None else act.beams,
          "ahead" : 1 if act.ahead is None else act.ahead,
          "stop" : tokenize(model, act.stop, False, False),
#         "vocab" : "TODO",
        })
    else:
        raise Exception()
    actions.update({ act.uid : action })

ftt = autocog.llama.evaluate(model, { 'actions' : actions })

print(ftt)

## cpp_reconstructed = detokenize(model, cpp_tokens, False, False)
