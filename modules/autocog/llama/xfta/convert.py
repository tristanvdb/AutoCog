
import math

from .xfta_cxx import tokenize, detokenize

from ...fta.actions import Choose, Text, Complete

def fta_to_cxx(model, fta, defaults, safe_mask, char_mask):
    actions = []
    for act in fta.actions.values():
        action = { "uid" : act.uid, "successors" : act.successors }
        if isinstance(act, Text):
            action.update({
              "__type__"  : "Text",
              "evaluate"  : defaults["Text"]["evaluate"] if act.evaluate is None else act.evaluate,
              "tokens"    : tokenize(model, act.text, False, False)
            })
        elif isinstance(act, Choose):
            if len(act.successors) == 1:
                action.update({ "successors" : [ act.successors[0] ] * len(act.choices) })
            assert len(act.successors) == 0 or len(action['successors']) == len(act.choices)
            action.update({
              "__type__"  : "Choose",
              "choices"   : [
                tokenize(model, choice[0], False, False) for choice in act.choices
              ],
              "threshold" : defaults["Choose"]["threshold"] if act.threshold is None else act.threshold,
              "width"     : defaults["Choose"]["width"] if act.width is None else act.width
            })
        elif isinstance(act, Complete):
            assert act.length is not None
            action.update({
              "__type__"  : "Complete",
              "length"    : act.length,
              "stop"      : tokenize(model, act.stop, False, False),
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
      "text" : detokenize(model, ftt["tokens"], False, False),
      "length" : ftt["length"],
      "logprobs" : ftt["logprobs"],
      "probability" : math.exp(-ftt["logprob"]/ftt["length"]),
      "locproba" : math.exp(-sum(ftt["logprobs"])/len(ftt["logprobs"])),
      "children" : [ cxx_to_ftt(model, child) for child in ftt["children"] ],
      "pruned" : ftt["pruned"],
    }

