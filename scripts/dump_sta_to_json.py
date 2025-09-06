#!/usr/bin/env python3

import sys, json, html, math, string, graphviz

from autocog.sta.compile import compile_source_to_program_and_stas
from autocog.sta.syntax import Syntax
from autocog.sta.runtime import Frame

from autocog.fta.automaton import FiniteThoughtAutomaton as FTA

from autocog.utility.models import loader

from autocog.llama.xfta import fta_defaults, fta_to_cxx, create_safe_vocab_mask, create_single_char_vocab

def main(argv):
    sta_file = argv[1]
    model_path = argv[2]
    json_data = argv[3] if len(argv) >= 4 else '{}'
    prompt_name = argv[4] if len(argv) >= 5 else 'main'

    sta = compile_source_to_program_and_stas(
      open(sta_file, 'r').read()
    )[1][prompt_name]

    (model, syntax) = loader(models_path=model_path, n_ctx=4096, use_cxx=True)

    fta = sta.instantiate(
      syntax=syntax,
      frame=Frame(
        state={ st.label() : None for st in sta.concretes.values() if st.abstract.field is not None },
        data=json.loads(json_data)
      ),
      branches={},
      inputs=None
    ).simplify()

    safe_mask = create_safe_vocab_mask(model.model)
    char_mask = create_single_char_vocab(model.model)
    cxx = fta_to_cxx(model.model, fta, fta_defaults, safe_mask, char_mask)

    with open(sta_file+'.json','w') as F:
        json.dump(cxx, F, indent=4)

if __name__ == '__main__':
    main(sys.argv)

