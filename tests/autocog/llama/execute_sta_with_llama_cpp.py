
import sys, json, html, math, string, graphviz

from autocog.sta.compile import compile_source_to_program_and_stas
from autocog.sta.syntax import Syntax
from autocog.sta.runtime import Frame

from autocog.fta.automaton import FiniteThoughtAutomaton as FTA

from autocog.utility.models import loader

import autocog.llama
from autocog.llama import tokenize, detokenize

def main(argv):
    sta_file = argv[1]
    json_data = argv[2]
    model_path = argv[3] if len(argv) >= 4 else ''
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

    (ftt, paths) = model.evaluate(fta)

    for (text, proba) in paths:
        print("========================================")
        print(f"[[ {proba} ]]")
        print("> " + "\n> ".join(text.split('\n')))

    ftt_to_graphviz_detailed(
        ftt, 
        output_filename='ftt', 
        format='svg',
        max_text_length=100,
        show_text_preview=True
    )

def ftt_to_graphviz_detailed(ftt, output_filename='ftt_tree_detailed', format='png', max_text_length=30, show_text_preview=True):
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

        if len(text) > max_len:
            text = text[:max_len] + "..."

        # HTML entity escaping
        text = text.replace('&', '＆')   # Must be first
        text = text.replace('<', '‹')
        text = text.replace('>', '›')
        text = text.replace('"', '&quot;')
        text = text.replace("'", '&#39;')

        # GraphViz special characters
        text = text.replace('\\', '\\\\')
        text = text.replace('{', '\\{')
        text = text.replace('}', '\\}')
        text = text.replace('|', '\\|')

        # Handle newlines and control characters
        text = text.replace('\n', '↵')
        text = text.replace('\r', '⏎')
        text = text.replace('\t', '→')

        # Remove remaining control characters
        text = ''.join(c if ord(c) >= 32 or c in ['\n', '\r', '\t'] else f'\\x{ord(c):02x}' for c in text)

        return text
    
    def add_node_recursive(node, parent_id=None, depth=0, is_best=True):
        node_id = f"node_{node_counter[0]}"
        node_counter[0] += 1
        
        # Extract node properties
        probability = node["probability"]
        locproba = node["locproba"]
        text = node.get("text", "")
        is_pruned = node["pruned"]
        
        # Format probability
        if probability < 0.001:
            prob_str = f"{probability:.2e}"
        elif probability < 0.01:
            prob_str = f"{probability:.4f}"
        else:
            prob_str = f"{probability:.3f}"

        if locproba < 0.001:
            locprob_str = f"{locproba:.2e}"
        elif locproba < 0.01:
            locprob_str = f"{locproba:.4f}"
        else:
            locprob_str = f"{locproba:.3f}"
        
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
<TR><TD BGCOLOR="{header_color}" ALIGN="CENTER" COLSPAN="2"><B>P = {prob_str}</B>(p = {locprob_str})</TD></TR>
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

