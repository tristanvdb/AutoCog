# STL Runtime Semantics & Execution Model

Clarifications from developer discussion to guide Stage 6 implementation.

## Execution Pipeline

```
STL source → Parser → AST → Stage 1-5 → Instantiation Graph
                                              ↓
                                    Stage 6: AST→IR Conversion
                                              ↓
                                         STA (IR/JSON)
                                              ↓
                                    Runtime: STA→FTA Unrolling
                                              ↓
                                         LLM Execution
```

## 1. Execution Model: STA → FTA → LLM

**Compile Time (Stage 6):**
- STL → STA (Structured Thoughts Automaton) IR
- STA is the compiled representation with prompts, fields, channels, flows

**Runtime (xFTA tool):**
- STA → FTA (Finite Thoughts Automaton) unrolling
- FTA is the actual state machine executed by LLM
- Each **prompt** = basic block in control flow graph
- Each **field** in prompt = FTA state for LLM to fill

**Example:**
```stl
prompt main {
  is {
    topic is text;              // → FTA state 1: completion
    question is text;           // → FTA state 2: completion
    answer is enum("A", "B", "C");  // → FTA state 3: choice(A,B,C)
  }
  return { from .answer; }
}
```

FTA creates 3 sequential states, then terminates.

## 2. Flows = Control Flow Branches

Flows create edges in the control flow graph between prompts (basic blocks).

**Semantics:**
- Prompts = basic blocks
- Flows = branch edges
- LLM chooses which flow to take at runtime

**Example 1: Choice between branches**
```stl
prompt step1 { ... }
prompt step2 { ... }

prompt main {
  flow { to step1; to step2; }
}
```
→ LLM must choose: execute step1 OR step2

**Example 2: Loop with bound**
```stl
prompt main {
  flow { to main[5] as "retry"; }
  return { ... }
}
```
→ LLM can choose: loop back to main (max 5 times) OR return

**Example 3: Multiple options**
```stl
flow {
  to process;
  to skip;
  to retry[3];
}
return { ... }
```
→ LLM chooses: process, skip, retry(max 3), or return

**Key Point:** Return statement provides termination edge. Without it, execution must flow somewhere.

## 3. Channels = Data Flow (Pre-computed)

Channels execute **BEFORE** prompt is unrolled to FTA. They populate fields with data.

### Channel Execution Order:

1. **Input channels:** Bring in external data
2. **Dataflow channels:** Pull from other prompt executions (Frame object)
3. **Call channels:** Invoke functions/prompts
4. **Then:** Unroll prompt to FTA with all data populated

**Critical:** Channels are pre-computation, not concurrent with LLM execution!

### Channel Types:

**Input:** External data
```stl
channel {
  to .topic from ?topic;     // '?' marks external input
  to .question from ?question;
}
```

**Dataflow:** From current or other prompts
```stl
channel {
  to .hint from .reflect;           // Internal: current prompt
  to .context from other.result;    // Cross-prompt: from 'other'
}
```

For cross-prompt dataflow, data pulled from last execution of that prompt (Frame).

**Call:** Function/prompt invocation
```stl
channel {
  to .result from call(process, input: .data, mode: "fast");
}
```

Calls can be:
- Python functions (extern): `call(module.function, ...)`
- Other prompts: `call(helper, ...)`

### Channel Dependency Resolution:

**STL:** Channels execute in **declaration order**
**Future:** Could analyze dependencies at compile time and reorder

**Example:**
```stl
channel {
  to .processed from call(clean, text: .raw);  // Must execute after next line
  to .raw from ?input;                          // Must execute first
}
```
Compiler should warn or reorder.

## 4. Records = Inlined Copies

Records are **templates** that get **copied** when inlined into prompts.

### Storage Strategy:

**Driver cache:**
```cpp
std::unordered_map<std::string, std::unique_ptr<ir::Record>> records;
```

Each unique (name, parameters) combination gets one entry:
- `"0::Address"` - Address with no parameters
- `"0::Data__length_i10"` - Data<10>
- `"0::Data__length_i20"` - Data<20>

**When inlining into prompt:**
1. Look up record template in cache by mangled name
2. **Copy** the record structure (fields)
3. Add copied fields to prompt's field list
4. Update field.parent to point to prompt (not template)

**Example:**
```stl
record Address {
  is {
    street is text;
    city is text;
  }
}

prompt p1 { is { addr1 is Address; } }
prompt p2 { is { addr2 is Address; } }
```

Result:
- `records["0::Address"]` = template with street, city fields
- `prompts["0::p1"]` = copy of Address fields inlined
- `prompts["0::p2"]` = separate copy of Address fields inlined

Each prompt gets its own copy of the record structure.

### Field Indexing:

Fields are indexed **per depth level**:

```stl
record Addr {
  is {
    street is text;   // depth=2, index=0
    city is text;     // depth=2, index=1
  }
}

prompt main {
  is {
    name is text;     // depth=1, index=0
    addr is Addr;     // depth=1, index=1
  }
}
```

Index **restarts at 0** for each depth level, not global.

## 5. STL Language Features

### Define Statements

**Purpose:** Compile-time variables/constants

```stl
define max_length = 50;
define mode = "strict";

record Data {
  is text<max_length>;  // Evaluated at compile time → text<50>
}
```

**Scope:**
- Global defines (file-level)
- Prompt/record-level defines (local scope)

**Evaluation:** All expressions are **compile-time constant evaluation**

**NOT:** Runtime variables (no mutable state)

### Unnamed Records (Inline Structs)

**Syntax:** Nest record directly in field definition

```stl
prompt main {
  is {
    // Named record reference:
    addr1 is Address;

    // Unnamed record (inline struct):
    addr2 {
      street is text;
      city is text;
    }
  }
}
```

Unnamed records create ad-hoc structure without separate record definition.

### Search (RAG Extension)

**Status:** Out of scope for initial implementation
**Purpose:** Typed retrieval using Records
**Parser:** Already present, ignore for now

### Assign

**Purpose:** Parameter/define assignment (compile-time only)
**Not:** Runtime variable mutation

### Return Statement

**Purpose:** Specify output fields and termination

```stl
prompt main {
  is {
    thinking is text;
    answer is text;
  }
  return {
    from .answer;  // Only return answer field, not thinking
  }
}
```

**Semantics:**
- Creates termination edge in control flow
- Specifies which fields to export to caller
- Optional alias: `return { as "success"; from .answer; }`

## 6. Python Function Calls

**IR Representation:**
```cpp
struct Call {
    std::optional<std::string> extern;  // "module.function"
    std::optional<std::string> entry;   // For prompt calls
    std::unordered_map<std::string, Kwarg> kwargs;
    std::optional<std::unordered_map<std::string, DocPath>> binds;
    DocPath target;  // Where result goes
};
```

**Module Resolution:**
- Store as string: "module.function"
- Link with file ID from import statement
- Validation: Not at compile time (runtime concern)

**Example:**
```stl
import python from "helper.py";

channel {
  to .result from call(python.process, input: .data);
}
```

## 7. Type/Expression Evaluation

**Rule:** All expressions evaluated at **compile time**

```stl
define max_length = 50;
define doubled = max_length * 2;

record Data {
  is text<doubled>;  // Compiled to: text<100>
}
```

**IR stores evaluated values:**
- `Completion(length=100)` NOT `Completion(length=doubled)`
- All format parameters are concrete values in IR

**Implications:**
- No symbolic references in IR
- Evaluator must resolve all expressions during Stage 6 conversion
- Runtime doesn't need expression evaluator

## 8. Annotations = Prompt Engineering

Annotations are **critical** for LLM execution. They generate prompt headers describing fields.

**Syntax:**
```stl
prompt main @ "You are answering questions." {
  is {
    question @ "the question to answer" is text;
    answer @ "your answer to the question" is text;
  }
}
```

**IR Storage:**
```cpp
struct Object {
    std::string name;
    std::vector<std::string> desc;  // Annotations here
};

struct Field : Object {
    // Inherits desc for field annotations
};

struct Prompt : Object {
    // desc contains prompt-level annotations
};
```

**Runtime Usage:**
Annotations assembled into prompt header for LLM:
```
You are answering questions.

Fields:
- question: the question to answer
- answer: your answer to the question
```

**Key Point:** Annotations are not debug info, they're part of the compiled output for LLM consumption.

## 9. Dataflow from Parameterized Prompts

**Problem:** Dataflow channels reference prompts by name, but parameterized prompts have multiple instantiations.

```stl
prompt worker<id> { is { result is text; } }

// worker<1>, worker<2>, worker<3> are all different instantiations

prompt collector {
  channel {
    to .data from worker<?>.result;  // Which worker?
  }
}
```

**Solution:** Use **mangled names** in channel IR

```stl
export worker<1> as w1;

prompt collector {
  channel {
    to .data from w1.result;  // Unambiguous
  }
}
```

**IR Representation:**
```cpp
struct Dataflow {
    std::optional<std::string> prompt;  // Mangled name: "0::worker__id_i1"
    DocPath src;
    DocPath tgt;
};
```

**Design Decision:** Store mangled names in IR. User must use aliases to disambiguate parameterized prompts in dataflow.

## 10. Field Parent Pointers

When records are inlined into prompts, field.parent points to the **containing prompt**, not the record template.

**Example:**
```stl
record Address {
  is {
    street is text;
    city is text;
  }
}

prompt main {
  is {
    addr is Address;
  }
}
```

**Result in IR:**
```cpp
Prompt main:
  fields = [
    Field(name="addr", format=nullptr, depth=1, index=0, parent=&main),
    Field(name="street", format=Completion(), depth=2, index=0, parent=Field("addr")),
    Field(name="city", format=Completion(), depth=2, index=1, parent=Field("addr")),
  ]
```

Parent chain: `city.parent → addr.parent → main`

This creates a proper tree structure for path resolution.

## Summary: Critical Points for Stage 6

1. ✅ Records use `unique_ptr` and are copied when inlined
2. ✅ Field indices restart per depth level
3. ✅ All expressions evaluated at compile time
4. ✅ Channels execute before FTA unrolling
5. ✅ Flows create control flow edges (branches)
6. ✅ Annotations are for LLM prompt engineering
7. ✅ Store mangled names in dataflow channels
8. ✅ Field.parent points to containing structure, not template
9. ✅ Python calls stored as strings with file linkage
10. ✅ Return creates termination edges

Next: Fix ir.hxx based on these semantics, then implement Stage 6.
