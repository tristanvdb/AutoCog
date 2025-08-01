
import sys, json

from autocog.sta.compile import compile_source_to_program_and_stas
from autocog.sta.syntax import Syntax
from autocog.sta.runtime import Frame
from autocog.fta.automaton import FiniteThoughtAutomaton as FTA

sta_file = sys.argv[1]
json_data = sys.argv[2]

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

for action in fta.actions.values():
    print(action)

