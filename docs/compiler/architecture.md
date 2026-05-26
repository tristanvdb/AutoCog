# STL Compiler Architecture

## Overview

The STL (Structured Thoughts Language) compiler transforms source files into executable IR (Intermediate Representation) that can be executed by the STA runtime.

## Compilation Pipeline

```
STL Source Files
    ↓
[Stage 1] Parse → AST
    ↓
[Stage 2] Symbol Collection → Symbol Table
    ↓
[Stage 3] Evaluate → Resolve Defines
    ↓
[Stage 4] Scan → Find Instantiations
    ↓
[Stage 5] Instantiate → Build Instantiation Graph
    ↓
[Stage 6] AST→IR → Generate IR (TODO)
    ↓
JSON Output → STA Format
```

## Compilation Stages

### Stage 1: Parse

Parses STL source files into an Abstract Syntax Tree (AST).

- **Input**: STL source files
- **Output**: `ast::Program` nodes
- **Implementation**: `libs/autocog/compiler/stl/parser.*`
- **Tests**: `tests/units/autocog/compiler/stl/parser/`

All 49 AST node types are implemented and tested.

### Stage 2: Symbol Collection

Collects all symbols (prompts, records, defines, imports) into a symbol table.

- **Input**: AST nodes
- **Output**: `SymbolTable` with qualified names
- **Implementation**: `symbol-table.{hxx,cxx}`, `symbol-scanner.{hxx,cxx}`

### Stage 3: Evaluate

Evaluates global define statements to constant values.

- **Input**: Symbol table + AST
- **Output**: `Driver::defines` map populated
- **Implementation**: `evaluate.{hxx,cxx}`

### Stage 4: Scan

Scans for all prompt/record instantiations (including parameterized types).

- **Input**: Symbol table + AST
- **Output**: List of instantiations to create
- **Implementation**: `instance-scanner.{hxx,cxx}`

### Stage 5: Instantiate

Builds instantiation graph with mangled names and parameter contexts.

- **Input**: Instantiation list
- **Output**: `InstantiationGraph` with nodes and edges
- **Implementation**: `instantiation-graph.{hxx,cxx}`
- **Features**:
  - Name mangling for parameterized types
  - BFS graph building
  - Cycle detection
  - Context propagation

### Stage 6: AST→IR Conversion

Converts AST + instantiation graph into executable IR structures.

- **Input**: Instantiation graph
- **Output**: IR structures (prompts, records, fields, channels, flows)
- **Target**: Populate `Driver::{prompts, records, entry_point_map}`

## Key Data Structures

### AST (Abstract Syntax Tree)
- **Location**: `libs/autocog/compiler/stl/ast.hxx`
- **Purpose**: Parse tree representation of STL source
- **Node Types**: 49 types covering all language constructs

### Symbol Table
- **Location**: `libs/autocog/compiler/stl/symbol-table.hxx`
- **Purpose**: Maps qualified names to symbol information
- **Key Types**: `PromptSymbol`, `RecordSymbol`, `DefineSymbol`, `UnresolvedAlias`, `UnresolvedImport`

### Instantiation Graph
- **Location**: `libs/autocog/compiler/stl/instantiation-graph.hxx`
- **Purpose**: Represents all type instantiations with parameters resolved
- **Structure**: Nodes (instantiations) with edges (dependencies)

### IR (Intermediate Representation)
- **Location**: `libs/autocog/compiler/stl/ir.hxx`
- **Purpose**: Executable representation for STA runtime
- **Key Types**: `Prompt`, `Record`, `Field`, `Channel`, `Format`
- **Recent Update**: Restructured to match Python STA implementation

## Testing

### Unit Tests
- **Parser Tests**: JSON-driven parser fragment tests
- **Location**: `tests/units/autocog/compiler/stl/parser/`

### Integration Tests
- **STL Programs**: Real STL files tested at each stage
- **Location**: `tests/integration/stl/`

### Demos
- **MCQ Examples**: Multiple-choice question demos
- **Location**: `share/demos/mcq/`
- **Purpose**: Real-world examples showcasing language features

## Build System

- **Build Tool**: CMake
- **Compiler**: C++17
- **Dependencies**:
  - RE/flex (lexer/parser)
  - nlohmann/json (JSON output)
  - pybind11 (Python bindings)

## Output Format

The compiler generates JSON in STA (Structured Thoughts Automaton) format:

```json
{
  "defines": { "var": value, ... },
  "entry_points": { "main": "0::prompt_name", ... },
  "records": { "0::RecordName": { ... }, ... },
  "prompts": { "0::PromptName": { ... }, ... }
}
```

