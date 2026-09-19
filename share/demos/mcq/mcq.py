"""Resolvers for the MCQ demos.

A frame records what the model emitted, verbatim: a `select` field holds the
INDEX the model produced, not the choice that index names. Turning one into
the other is the program's job, done here on the return — which is why nothing
in the runtime rewrites a field on the way out. A frame that said "Water"
where "1" was emitted would describe no token stream, and forcing it back
would mean inverting a transformation the frame no longer records.

Each function takes the prompt's frame spread as keyword arguments
(`return call pick _;`), so `**kwargs` absorbs the fields it does not read.

The `select` resolvers also take `zero_index`, bound at the call site from
`__syntax__.prompt_zero_index`. They need it because `select` emits a
POSITION and the syntax decides whether that position is spelled from 0 or
from 1 -- it sets the candidate values the model may emit, not just the
`[i]` on a label. Reading the emitted text as a 0-based index under a
one-indexed syntax names the NEXT choice along, and raises only when the
model happens to pick the last one.

It has no default on purpose. A default is how this went unnoticed: every
answer came back plausible.
"""


def pick(choices, answer, zero_index, **kwargs):
    """`select(choices)` over the list of strings: the index names a choice.

    `choices` is what the program's channel pulled out of the record list, so
    this sees texts even though the run's input was `choice`."""
    return choices[int(answer) - (0 if zero_index else 1)]


def pick_value(choice, answer, zero_index, **kwargs):
    """`select(choice)` over the record list: the index names a record, and
    the answer is that record's `value`."""
    return choice[int(answer) - (0 if zero_index else 1)]["value"]


def by_label(choice, answer, **kwargs):
    """`repeat(choice.label)`: the model emitted one of the labels present in
    the data, so the answer is the value carried by the record wearing it.

    The label cannot be out of range — `repeat` ranks the labels that exist,
    so there is no letter the model can produce that names nothing.
    """
    for c in choice:
        if c["label"] == answer:
            return c["value"]
    raise ValueError(f"no choice labelled {answer!r}")
