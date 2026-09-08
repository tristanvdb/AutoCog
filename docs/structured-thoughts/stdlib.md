# Standard Library

The stdlib ships with the autocog package. Its components are imported by path
relative to the stdlib root (which is always on the include path):

```
from "thinking/thoughts.stl" import Thought, reflexion;
from "datastore.py" import store, retrieve;
```

## thinking/ — Thinking Library

Reusable thinking primitives and patterns: chain-of-thought, reflection,
iterative refinement, planning, and deliberation. One pattern per file;
`thinking/thoughts.stl` is an umbrella that re-exports everything. See
[share/library/stlib/thinking/README.md](../../share/library/stlib/thinking/README.md)
for the full catalog and the conventions the patterns follow.

| File | Exports | Pattern |
|------|---------|---------|
| `thinking/thought.stl` | `Thought` | The core parameterized reasoning record |
| `thinking/reflexion.stl` | `reflexion` | Refine an initial attempt through private work |
| `thinking/brainstorm.stl` | `brainstorm` | Divergent idea generation |
| `thinking/critique.stl` | `Critique`, `critique` | Structured review of a draft |
| `thinking/revise.stl` | `revise` | Apply feedback to a draft (pairs with critique) |
| `thinking/decompose.stl` | `Step`, `decompose` | Plan-then-act task breakdown |
| `thinking/hypothesize.stl` | `Hypothesis`, `hypothesize` | Guess-then-check with explicit confidence |
| `thinking/deliberate.stl` | `Perspective`, `deliberate` | Multi-perspective debate and synthesis |

### Thought

A parameterized record for a single unit of reasoning.

```
record Thought {
  argument length = 20;      // token length
  argument mode = "simple";  // style descriptor
  argument goal = "show your work";  // purpose

  is text<length=length>;
  annotate f"A {mode} thought to {goal}.";
}
```

**Usage:**

```
// Default
steps[1:5] is Thought;

// Customized
analysis[3] is Thought<length=50, mode="analytical", goal="evaluate the evidence">;

// As an alias
alias Thought<mode="rough", goal="brainstorm"> as BrainstormThought;
ideas[1:5] is BrainstormThought;
```

### reflexion

A parameterized prompt that takes rough input and iteratively refines it through a working-thinking-response cycle.

```
prompt reflexion {
  argument subject;
  argument length = 20;
  argument mode = "simple";
  argument goal = "show your work";
  argument num_steps = 10;
  argument len_steps = 20;
  argument min_response = 3;
  argument max_response = 10;

  is {
    task[1:10] is text;
    initial[1:10] is text;
    work[1:num_steps] is Thought<length=len_steps, mode="working", goal="prepare a response">;
    response[min_response:max_response] is Thought<length=length, mode=mode, goal=goal>;
  }
  channel {
    task get task;
    initial get initial;
  }
  return {
    subject is f"Thought about {subject}";
    intermediate use work;
    response use response;
  }
}
```

**Inputs:** `task` (what to do) and `initial` (starting material).

**Output:** `response` (refined thoughts) and `intermediate` (working steps).

**Usage:**

```
channel {
  result call reflexion<
    subject="Refine the story outline",
    length=50,
    mode="refined",
    goal=f"produce a polished outline",
    num_steps=10
  > {
    task use plan.tasks;
    initial use draft.outline;
  } bind(_, response);
}
```

## datastore.py

In-memory key-value store for persisting data across prompts within a single execution.

### store

Store a value under a `(pkey, skey)` key pair. `data` is positional; `pkey` and
`skey` (a sub-key) both default to `""`. Returns the key for confirmation.

```python
def store(data, pkey="", skey=""):
    """Store data under (pkey, skey). Returns {"pkey": pkey, "skey": skey}."""
```

**STL usage:**

```
channel {
  done call store {
    pkey is "book";
    data use draft.book;
  };
}
```

### retrieve

Retrieve a previously stored value, or `None` if absent.

```python
def retrieve(pkey="", skey=""):
    """Retrieve the value stored under (pkey, skey), or None."""
```

**STL usage:**

```
channel {
  book call retrieve {
    pkey is "book";
  };
}
```

The datastore is scoped to a single program execution (one `engine.run()` call). It resets between runs.
