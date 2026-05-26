# Structured Thoughts — Overview

Structured Thoughts is a programming model for building applications driven by auto-regressive language models. Instead of treating LLMs as black-box text generators, Structured Thoughts defines programs where:

- **Prompts** are the unit of computation (like functions)
- **Automata** constrain the model's output to guarantee parseable structured data
- **Channels** define how data flows between prompts and external tools
- **Flows** control branching between prompts — decided by the model

## The Problem

A raw LLM prompt produces free-form text. Extracting structured data from it is fragile: parsing heuristics break, JSON output is unreliable, and multi-step reasoning requires ad-hoc orchestration code.

Structured Thoughts solves this by compiling prompt definitions into automata that guide the model's generation token-by-token. The model can only produce text that matches the declared structure. There is no parsing ambiguity because the structure is enforced during generation, not recovered afterward.

## Key Concepts

### Prompts as Functions

A prompt is like a function: it has typed fields (parameters and return values), receives input through channels, and produces structured output. Multiple prompts compose into programs through dataflow and control flow.

```
prompt summarize {
  is {
    document is text<length=500>;
    summary is text<length=100>;
    confidence is enum("high", "medium", "low");
  }
  channel {
    document get document;
  }
  return {
    use summary;
  }
}
```

### Automata for Structured Output

Each prompt compiles to a state machine (STA — Structured Thought Automaton). At runtime, the STA is instantiated with input data and formatting rules into an FTA (Formal Text Automaton) — a token-level automaton that the model executes.

The FTA constrains the model at each step: for a `text<length=20>` field, exactly 20 tokens of free text are generated. For an `enum("yes", "no")`, only those two tokens are valid. For `select(choices)`, the model picks an index from the available options.

This means the output always matches the declared schema. No parsing failures, no malformed JSON, no "I'll answer in a different format."

### Channels: Data Movement

Data enters a prompt through channels. Three kinds:

- **get** — reads from external inputs (provided by the caller)
- **use** — reads from another prompt's output (dataflow between prompts)
- **call** — invokes an external function (Python) or another prompt, returns the result

Channels are resolved before the prompt runs. The model never sees channel mechanics — it sees a formatted prompt with all data filled in.

### Flows: Model-Driven Branching

After a prompt completes, the program decides what to do next. This is where the model's judgment drives control flow:

- **Unconditional flow** — always go to the next prompt
- **Conditional flow** — the model's output determines which prompt runs next
- **Bounded loops** — repeat a prompt up to N times (the model decides when to stop)
- **Return** — exit and return values to the caller

```
prompt review {
  is {
    analysis is text<length=200>;
    verdict is enum("approve", "revise", "reject");
  }
  flow {
    revise_prompt[3] as "revise";  // up to 3 revision attempts
    done as "approve";
    done as "reject";
  }
}
```

### Records: Reusable Types

Records define reusable field structures. They can be parameterized for flexibility:

```
record Thought {
  argument length = 20;
  argument mode = "simple";
  is text<length=length>;
  annotate f"A {mode} thought.";
}

prompt reason {
  is {
    steps[1:5] is Thought<length=50, mode="analytical">;
    conclusion is text<length=100>;
  }
}
```

## Compilation Pipeline

```
STL source → Parse → Symbols → Instantiate → Assemble → Generate → STA
                                                                     ↓
                                                              Runtime: STA + inputs → FTA
                                                                     ↓
                                                              Model: FTA → text
                                                                     ↓
                                                              Parse: text → structured data
```

The STL compiler (`stlc` or `autocog compile`) produces an STA — a JSON representation of the program. At runtime, the STA is combined with input data and formatting syntax to produce an FTA that the model evaluates. The model's output is parsed back into structured fields using the same automaton that generated it.

## Comparison

| Approach | Structure guarantee | Multi-step | Tool integration |
|----------|-------------------|------------|-----------------|
| Raw prompting | None (hope for the best) | Manual orchestration | Ad-hoc |
| JSON mode | Partial (schema validation) | Manual | Ad-hoc |
| Function calling | API-defined | Framework-specific | Framework-specific |
| **Structured Thoughts** | **Compile-time guarantee** | **Language-level flows** | **Channel system** |
