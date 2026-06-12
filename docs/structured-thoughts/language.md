# STL Language Reference

STL (Structured Thought Language) defines prompts, records, and their composition into programs.

## Records

Records define reusable field structures.

```
record Name {
  is <field-type>;
}
```

A record can contain a single field or multiple named fields:

```
// Single-field record (inlines to its field type)
record Sentence {
  is text<length=50>;
}

// Multi-field record
record Analysis {
  is {
    reasoning is text<length=200>;
    confidence is enum("high", "medium", "low");
    score is text<length=5>;
  }
}
```

### Parameterized Records

Records can declare arguments with defaults:

```
record Thought {
  argument length = 20;
  argument mode = "simple";
  argument goal = "show your work";

  is text<length=length>;
  annotate f"A {mode} thought to {goal}.";
}
```

Used with explicit parameters:

```
steps[1:5] is Thought<length=50, mode="analytical", goal="reason carefully">;
```

### Aliases

Create named specializations:

```
alias Thought<mode="rough", goal="brainstorm"> as BrainstormThought;
```

## Field Types

### text

Free-form text generation, up to `length` tokens (the model may stop earlier).
The only structural parameters are `length` and `vocab`:

```
name is text;                         // unbounded
name is text<length=50>;              // up to 50 tokens
name is text<length=8, vocab=alnum>;  // restricted to a vocabulary
```

Sampling/search controls (`threshold`, `beams`, `ahead`, `width`, …) are **search
policies**, not field parameters — set them in a `search { }` block (see
[Search Policies](#search-policies)). Vocabularies are declared with `vocab` (see
[Vocabularies](#vocabularies)).

### enum

Pick one value from a fixed set. Values are quoted strings.

```
sentiment is enum("positive", "negative", "neutral");
```

Enum values follow the same string literal rules — single-line, single or double quotes.

### select / repeat (choice)

Pick from a dynamic array of options.

```
choices[4] is text<length=40>;
answer is select(choices);    // returns index (0, 1, 2, 3)
answer is repeat(choices);    // returns the text of the chosen item
```

### Arrays

Fields can be arrays with fixed or variable size:

```
items[5] is text<length=20>;        // exactly 5 items
items[1:10] is text<length=20>;     // 1 to 10 items (model decides)
items[0:5] is Thought;              // 0 to 5 items (optional)
```

### Nested Records

Fields can use record types for nesting:

```
record Page {
  is {
    title is text<length=30>;
    body is text<length=200>;
  }
}

prompt draft {
  is {
    book is {
      title is text<length=50>;
      pages[5] is Page;
    }
  }
}
```

## Vocabularies

A vocabulary constrains a `text` field to a subset of the model's tokens.
Declare one with `vocab`, then reference it from a field via `text<vocab=...>`.

```
vocab digit = tokenize("0", "1", "2", "3", "4", "5", "6", "7", "8", "9");  // explicit tokens
vocab alpha = regex("^[a-zA-Z]+$");   // tokens matching a regex
vocab universe = (!tokenize());       // complement of the empty set = all tokens
```

Vocabularies compose with set algebra — union `|`, intersection `&`, difference
`-`, and complement `!` — and may reference other vocabularies (inlined at
compile time):

```
vocab alnum = (digit | alpha);
vocab no_digits = (universe - digit);
vocab shared = (alnum & digit);
```

Reference them from fields, including inline compositions:

```
code is text<length=8, vocab=alnum>;
pin  is text<length=4, vocab=(digit - tokenize("0"))>;
```

Identical vocabulary expressions are deduplicated into a single entry in the
compiled artifact.

## Prompts

Prompts are the execution unit. They define fields (what the model generates), channels (data input), flows (what happens next), and annotations (instructions to the model).

```
prompt name {
  is { ... }          // fields
  channel { ... }     // data input
  flow { ... }        // control flow (optional)
  return { ... }      // return values (optional, alternative to flow)
  annotate { ... }    // model instructions (optional)
}
```

### Parameterized Prompts

Like records, prompts can have arguments:

```
prompt reflexion {
  argument subject;
  argument num_steps = 10;

  is {
    work[1:num_steps] is Thought;
    response[3:10] is text<length=50>;
  }
  // ...
}
```

## Channels

Channels define how data enters a prompt. They are resolved before the prompt runs — the model sees the formatted result, not the channel mechanics.

### get — External Input

Read from the caller-provided inputs.

```
channel {
  topic get topic;           // read "topic" from external inputs
  question get question;
}
```

### use — Dataflow

Read from another prompt's completed output.

```
channel {
  topic use init_idea.topic;        // from another prompt's field
  book.title use previous.book.title;  // nested path
  hints use .reflect;               // self-reference (previous iteration)
}
```

### call — External Invocation

Invoke a Python function or another prompt. Arguments are provided as keyword arguments, each sourced via `get` (external input), `use` (dataflow), or literal values.

```
channel {
  // Call a Python function
  templates call list_templates {
    age use .age;                    // kwarg from dataflow
  };

  // Call another prompt
  topic call reflexion<length=50, mode="refined"> {
    task use init_task.task;         // kwarg from dataflow
    initial use init_idea.idea;
  } bind(_, response);              // extract "response" from result

  // Call with literal value
  done call store {
    pkey is "book";                  // literal kwarg
    data use draft.book;
  };

  // Mapped call (invoked once per element)
  pages call create_page {
    step use task.steps mapped;      // iterate over array
  } ravel;                           // flatten results into array
}
```

### Channel Clauses

Clauses reshape a channel's resolved value before it becomes the field. They
apply to `get`, `use`, and `call` channels — not only calls — and to `return`
fields (see [Return](#return)).

| Clause | Form | Effect |
|--------|------|--------|
| `bind` | `bind(source)` · `bind(source, target)` | Select field `source` from the result, optionally renaming it to `target`. |
| `prune` | `prune(field)` | Drop the named `field` from the result. |
| `ravel` | `ravel` · `ravel(depth)` · `ravel(depth, target)` | Flatten nested array results (optional `depth`, optional `target`). |
| `mapped` | `mapped` · `mapped(target)` | Read/invoke once per element of the source array. |
| `wrap` | `wrap` · `wrap(target)` | Wrap a scalar result into a single-element array. |

`bind` and `prune` require their first argument; the parenthesized arguments of
`ravel`, `mapped`, and `wrap` are optional.

## Flows

Flows define what happens after a prompt completes.

### Unconditional

```
flow next_prompt;                   // always go to next_prompt
```

### Conditional (with loop bound)

```
flow {
  edit_prompt[5] as "edit";        // up to 5 times, label "edit"
  done as "done";                   // alternative exit
}
```

The model's output determines which branch is taken. The label string (e.g., `"edit"`) is matched against the model's completion of a flow-selection field.

### Return

A `return` ends the prompt and produces its output — short form (one field),
block form, or empty:

```
return use answer;                  // short form: one field

return {
  status is "complete";             // literal value
  result use computed_value;        // from a field
  score use analysis.confidence;    // nested path
}

return;                             // empty return (no output)
```

A return may carry a **label**, matched like a flow branch when the prompt is
reached by a labeled flow:

```
return done use answer;             // labeled return "done"
```

Return fields accept the same `bind` / `ravel` / `wrap` / `prune` clauses as
channels (see [Channel Clauses](#channel-clauses)).

## Annotations

Instructions to the model, rendered as part of the prompt text. All annotation values are single-line strings (see [String Literals](#string-literals)).

```
annotate {
  _ as "You are a helpful assistant answering questions.";
  question as "the question you need to answer";
  choices as "the available options, only one is correct";
  answer as "pick the best option";
}
```

The `_` annotation applies to the prompt as a whole. Named annotations apply to specific fields. F-strings can reference arguments:

```
annotate f"A {mode} thought to {goal}.";
```

### File-level annotations (not yet implemented)

A bare `annotate` at file scope is intended as a program-wide instruction —
prepended to the top of every prompt's rendered text (where the active syntax
places it):

```
annotate "You are a careful assistant.";   // intended: applies to every prompt
```

> **Not yet implemented.** A top-level `annotate` currently parses but has no
> effect. Until it is wired through the compiler, repeat a `_` annotation in each
> prompt instead.

## Search Policies

A `search { }` block tunes how the runtime explores generations — sampling
threshold, beam search, lookahead, pruning width, and queue ordering. These are
*policies*, distinct from a field's structural type. Parameters are namespaced
by category:

| Category | Applies to | Example params |
|----------|------------|----------------|
| `text.*`   | text completions   | `threshold`, `beams`, `ahead`, `width` |
| `enum.*`   | enum fields        | `threshold`, `width` |
| `branch.*` | flow branching     | `threshold`, `width` |
| `flow.*`   | flow control       | `threshold`, `width` |
| `queue.*`  | global work queue  | `metric` |

Right-hand sides are compile-time expressions:

```
search {
  text.threshold is 0.2;
  text.beams is 4;
  enum.width is 3;
  queue.metric is "perplexity";
}
```

A `search` block may appear at **file scope** (program-wide defaults), inside a
**record**, inside a **prompt**, or inside an **inline struct**, with the
innermost scope winning. Policies are carried opaquely into the STA; defaults
and resolution are applied at instantiation.

## Imports and Exports

### Imports

Import records, prompts, aliases, or Python externals from other files:

```
from "thoughts.stl" import Thought, reflexion;
from "datastore.py" import store, retrieve;
from "template.py" import list_templates;
```

The compiler searches include paths in order: explicit `-I` paths, app-local, then the installed stdlib.

### Exports

Declare which prompts are entry points:

```
export init_idea as main;
export loop_cond as edit;
```

Each export creates a named entry point with auto-generated input/output schemas.

## Defines

Compile-time constants:

```
define max_length = 100;
define default_mode = "concise";

record R {
  is text<length=max_length>;
}
```

Can be overridden from the command line: `stlc -Dmax_length=200 program.stl`.

## String Literals

Strings use double or single quotes:

```
annotate "You are a helpful assistant.";
annotate 'You are a helpful assistant.';
```

### Escape Sequences

Use backslash to include quotes inside strings:

```
annotate "He said \"hello\" to the group.";
annotate 'It\'s a simple question.';
```

Alternatively, use the other quote type to avoid escaping:

```
annotate 'He said "hello" to the group.';
annotate "It's a simple question.";
```

### Limitations

**No multi-line strings.** Each string literal must be on a single line. There is no triple-quote, heredoc, or line-continuation syntax.

```
// ERROR: string cannot span lines
annotate "This is a long annotation
that spans multiple lines.";

// WORKAROUND: use multiple annotations on the same field,
// or keep the text on one line
annotate "This is a long annotation that stays on one line.";
```

**No string concatenation.** Two adjacent strings are not joined. Use f-strings to compose text from parts.

## F-strings

String interpolation using compile-time values:

```
annotate f"Write a story for a {age} year old about {topic}.";
```

Works in annotations and literal values. References must resolve to compile-time constants or prompt/record arguments.
