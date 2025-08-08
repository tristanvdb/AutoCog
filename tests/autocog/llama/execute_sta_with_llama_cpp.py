
import sys, json, html, math, graphviz

from autocog.sta.compile import compile_source_to_program_and_stas
from autocog.sta.syntax import Syntax
from autocog.sta.runtime import Frame

from autocog.fta.automaton import FiniteThoughtAutomaton as FTA
from autocog.fta.actions import Choose, Text, Complete

import autocog.llama
from autocog.llama import tokenize, detokenize

fta_defaults = {
  "Text" : {},
  "Choose" : {
    "threshold" : 0e-10,
    "width" : 3
  },
  "Complete" : {
    "threshold" : 0e-8,
    "width" : 3,
    "beams" : 2,
    "ahead" : 1
  }
}

def main(argv):
    sta_file = argv[1]
    json_data = argv[2]
    model_path = argv[3]

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
    ).simplify()

    model = autocog.llama.create(model_path, 4096) if len(model_path) > 0 else 0

    ftt = cxx_to_ftt(model, autocog.llama.evaluate(model, fta_to_cxx(model, fta, fta_defaults)))

#    print(json.dumps(ftt, indent=4))
    paths = extract_paths_from_ftt(ftt)
    paths = sorted(paths, key=lambda x: x[1])
    for (text, proba) in paths:
        print("========================================")
        print(f"[[ {proba} ]]")
        print("> " + "\n> ".join(text.split('\n')))

    ftt_to_graphviz_detailed(
        ftt, 
        output_filename='ftt', 
        format='svg',
        max_text_length=40,
        show_text_preview=True
    )

def fta_to_cxx(model, fta, defaults):
    actions = []
    for act in fta.actions.values():
        action = { "uid" : act.uid, "successors" : act.successors }
        if isinstance(act, Text):
            action.update({
              "__type__"  : "Text",
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
#             "vocab"     : "TODO",
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
#    print(f'#{ftt["action"]}: {ftt["tokens"]}')
    return {
      "action" : ftt["action"],
      "text" : detokenize(model, ftt["tokens"], False, False),
      "length" : ftt["length"],
      "logprobs" : ftt["logprobs"],
      "logprob" : ftt["logprob"],
      "probability" : math.exp(-ftt["logprob"]),
      "normprob" : math.exp(-ftt["logprob"]/ftt["length"]),
      "probabilities" : [ math.exp(-lpb) for lpb in ftt["logprobs"] ],
      "children" : [ cxx_to_ftt(model, child) for child in ftt["children"] ],
      "pruned" : ftt["pruned"],
    }

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
    return paths


def ftt_to_graphviz_detailed(ftt, output_filename='ftt_tree_detailed', format='png', 
                            max_text_length=30, show_text_preview=True):
    """
    Detailed version with HTML-like labels for better formatting.
    """
    dot = graphviz.Digraph(comment='FTT Tree Detailed', format=format)
    dot.attr(rankdir='TB')
    # Important: use 'plaintext' shape for HTML labels
    dot.attr('node', shape='plaintext')
    
    node_counter = [0]
    
    def format_text_for_display(text, max_len):
        """Format text for display in node, handling newlines and long text."""
        if not text:
            return "[empty]"
        
        # Replace newlines with spaces for display
        text = text.replace('\n', ' ').replace('\r', '')
        
        # Escape HTML characters
        text = html.escape(text)
        
        # Truncate if needed
        if len(text) > max_len:
            text = text[:max_len] + "..."
        
        return text
    
    def add_node_recursive(node, parent_id=None, depth=0, is_best=True):
        node_id = f"node_{node_counter[0]}"
        node_counter[0] += 1
        
        # Extract node properties
        probability = node["probability"]
        text = node.get("text", "")
        is_pruned = node["pruned"]
        
        # Format probability
        if probability < 0.001:
            prob_str = f"{probability:.2e}"
        elif probability < 0.01:
            prob_str = f"{probability:.4f}"
        else:
            prob_str = f"{probability:.3f}"
        
        # Determine colors
        if is_pruned:
            header_color = "#FFB6C1"  # lightpink
            border_color = "#FF0000"  # red
        elif probability > 0.01:
            header_color = "#90EE90"  # lightgreen
            border_color = "#008000"  # green
        elif probability > 0.0001:
            header_color = "#ADD8E6"  # lightblue
            border_color = "#0000FF"  # blue
        else:
            header_color = "#FFFFE0"  # lightyellow
            border_color = "#FFA500"  # orange
        
        # Format text for display
        display_text = format_text_for_display(text, max_text_length)
        
        label = f'''<<TABLE BORDER="2" CELLBORDER="1" CELLSPACING="0" CELLPADDING="4" COLOR="{border_color}">
<TR><TD BGCOLOR="{header_color}" ALIGN="CENTER" COLSPAN="2"><B>P = {prob_str}</B></TD></TR>
<TR><TD COLSPAN="2" ALIGN="LEFT" BGCOLOR="white"><FONT FACE="monospace">{display_text}</FONT></TD></TR>
</TABLE>>'''
        
        # Add node with HTML label
        dot.node(node_id, label)
        
        # Add edge from parent
        if parent_id is not None:
            edge_style = 'dashed' if is_pruned else 'solid'
            edge_color = 'red' if is_pruned else 'black'

            edge_width = "4.0" if is_best else "1.0"
        
            dot.edge(parent_id, node_id, style=edge_style, color=edge_color, penwidth=edge_width)
        
        # Recursively add children
        if "children" in node:
            for child in sorted(node["children"], key=lambda x: x["probability"], reverse=True):
                add_node_recursive(child, node_id, depth + 1, is_best)
                is_best = False
        
        return node_id
    
    # Build the tree
    add_node_recursive(ftt)
    
    # Render
    dot.render(output_filename, view=False, cleanup=True)
    
    return dot

if __name__ == '__main__':
    main(sys.argv)

