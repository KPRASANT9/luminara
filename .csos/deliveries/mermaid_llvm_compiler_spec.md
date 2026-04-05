# Mermaid→LLVM Compiler System Specification

## Living Diagram Compilation for Human Operation

**System Name**: DiagramVM  
**Version**: 1.0  
**Status**: Architecture Specification  
**Date**: April 2026

---

## 1. Conceptual Overview

### 1.1 The Vision

A **living diagram** is a Mermaid specification that compiles to native executable code via LLVM, enabling:

- **Interactive execution**: Click nodes to trigger actions
- **Real-time computation**: Diagrams that calculate and display results
- **Event-driven behavior**: Respond to user input, timers, data changes
- **Live visualization**: Animations and state changes during execution

### 1.2 Compilation Pipeline

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│  .md File   │───▶│   Lexer     │───▶│   Parser    │───▶│   AST       │───▶│    LLVM     │
│ (Mermaid)   │    │ (Tokens)    │    │ (AST Build) │    │ (Semantics) │    │    IR       │
└─────────────┘    └─────────────┘    └─────────────┘    └─────────────┘    └─────────────┘
                                                                          │
                                                                          ▼
                                                                   ┌─────────────┐
                                                                   │   LLVM      │
                                                                   │  JIT/Compile│
                                                                   └─────────────┘
                                                                          │
                                                                          ▼
                                                                   ┌─────────────┐
                                                                   │  Runtime    │
                                                                   │ (Interactive)│
                                                                   └─────────────┘
```

---

## 2. Core Components

### 2.1 Component Architecture

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                           DiagramVM Compiler Stack                          │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │                        FRONTEND LAYER                                │    │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐            │    │
│  │  │  Markdown    │  │    Live      │  │   Visual     │            │    │
│  │  │  Editor      │  │   Preview    │  │   Debugger   │            │    │
│  │  └──────────────┘  └──────────────┘  └──────────────┘            │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                    │                                         │
│                                    ▼                                         │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │                      COMPILER LAYER                                 │    │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐            │    │
│  │  │    Lexer     │  │    Parser    │  │   Semantic   │            │    │
│  │  │  (mermaid.ll)│  │ (mermaid.y)  │  │   Analyzer   │            │    │
│  │  └──────────────┘  └──────────────┘  └──────────────┘            │    │
│  │         │                │                 │                      │    │
│  │         ▼                ▼                 ▼                      │    │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐            │    │
│  │  │   Token      │  │     AST      │  │   Symbol     │            │    │
│  │  │   Stream     │  │   Builder    │  │    Table     │            │    │
│  │  └──────────────┘  └──────────────┘  └──────────────┘            │    │
│  │                                                                  │    │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐            │    │
│  │  │     IR       │  │   Optimizer  │  │   Codegen    │            │    │
│  │  │  Generator   │  │   (opt -O3)  │  │   (LLVM)     │            │    │
│  │  └──────────────┘  └──────────────┘  └──────────────┘            │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                    │                                         │
│                                    ▼                                         │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │                      RUNTIME LAYER                                   │    │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐            │    │
│  │  │   JIT        │  │   Event      │  │   Render     │            │    │
│  │  │  Executor    │  │   Loop       │  │   Engine     │            │    │
│  │  └──────────────┘  └──────────────┘  └──────────────┘            │    │
│  │         │                │                 │                      │    │
│  │         ▼                ▼                 ▼                      │    │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐            │    │
│  │  │  Function    │  │   Handler    │  │   Canvas     │            │    │
│  │  │  Registry   │  │   Registry   │  │   (WebGL)     │            │    │
│  │  └──────────────┘  └──────────────┘  └──────────────┘            │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Component Specifications

### 3.1 Lexer (Tokenization)

**Purpose**: Convert Mermaid text into tokens

**Location**: `src/compiler/lexer/`

**Token Types**:

```cpp
enum class TokenType {
    // Diagram type keywords
    GRAPH, DIGRAPH, FLOWCHART, SEQUENCE, CLASS, STATE, ER, GANTT, PIE,
    
    // Structure
    ID, STRING, NUMBER, DIRECTION_LR, DIRECTION_TD,
    
    // Operators
    ARROW, DASH_ARROW, LINE, THICK_LINE, DOTTED_LINE,
    
    // Brackets
    LBRACE, RBRACE, LBRACKET, RBRACKET, LPAREN, RPAREN,
    
    // Special
    COMMENT, STYLE_DEF, LINK_STYLE, CLASS_DEF,
    
    // Interactive extensions
    ONCLICK, ONHOVER, ONENTER, ONLEAVE,
    COMPUTE, WHEN, THEN, IF, ELSE,
    
    EOF, ILLEGAL
};
```

**Key Files**:
- `lexer.h` - Token and Lexer class definitions
- `lexer.cpp` - Implementation using Flex or manual implementation
- `location.h` - Source location tracking for error messages

**Example Tokenization**:

```
Input:  "A --> B{condition}"
Output: [ID:"A", ARROW, ID:"B", LBRACE, ID:"condition", RBRACE]
```

### 3.2 Parser (AST Construction)

**Purpose**: Build Abstract Syntax Tree from tokens

**Location**: `src/compiler/parser/`

**AST Node Types**:

```cpp
// Base node
struct AstNode {
    Location loc;
    NodeType type;
};

// Diagram structure
struct GraphNode : AstNode {
    std::string id;
    Direction direction;  // LR, TD, RL, BT
    std::vector<Statement*> statements;
};

// Node definition
struct NodeStmt : AstNode {
    std::string node_id;
    std::string label;
    std::string shape;    // box, circle, diamond, etc.
    std::vector<StyleProp> styles;
    std::vector<EventHandler> events;
};

// Edge definition  
struct EdgeStmt : AstNode {
    std::string from;
    std::string to;
    EdgeType type;        // solid, dotted, thick
    std::string label;
    std::vector<StyleProp> styles;
};

// Interactive: Click handler
struct ClickHandler : AstNode {
    std::string node_id;
    std::string action;  // navigate, compute, alert, custom
    std::string expr;    // Expression to evaluate
};

// Computed property
struct ComputeProp : AstNode {
    std::string target;  // node or edge property
    std::string expr;    // LLVM IR expression
    EvalType eval_time;  // compile, render, click, interval
};
```

**Key Files**:
- `parser.h` - Parser class
- `parser.cpp` - Implementation using Bison or manual
- `ast.h` - AST node definitions
- `ast_visitor.h` - Visitor pattern for tree traversal

### 3.3 Semantic Analyzer

**Purpose**: Validate AST and gather symbol information

**Location**: `src/compiler/semantic/`

**Analysis Tasks**:

| Check | Description | Error Code |
|-------|-------------|------------|
| Undefined References | Nodes referenced but not defined | E001 |
| Duplicate Definitions | Same ID defined multiple times | E002 |
| Type Mismatches | Edge types inconsistent | E003 |
| Cycle Detection | Infinite loop in graph | E004 |
| Scope Validation | Event handlers reference valid nodes | E005 |
| Expression Validation | Compute expressions type-check | E006 |

**Symbol Table Structure**:

```cpp
class SymbolTable {
    std::unordered_map<std::string, Symbol> symbols;
    std::vector<std::unordered_map<std::string, Symbol>> scopes;
    
public:
    void enterScope();
    void exitScope();
    void define(const std::string& name, Symbol symbol);
    Symbol* lookup(const std::string& name);
    std::vector<Symbol*> getAllInScope();
};

struct Symbol {
    std::string name;
    SymbolKind kind;  // node, edge, class, function
    Type* type;
    AstNode* decl;    // Pointer to AST declaration
    Scope* scope;
};
```

### 3.4 LLVM IR Generator

**Purpose**: Convert AST to LLVM Intermediate Representation

**Location**: `src/compiler/codegen/`

**IR Structure**:

```cpp
class MermaidModule {
    llvm::Module* module;
    llvm::IRBuilder<>* builder;
    SymbolTable* symbols;
    
public:
    // Generate function for each click handler
    llvm::Function* generateClickHandler(const ClickHandler& handler);
    
    // Generate function for computed properties
    llvm::Function* generateComputeFunc(const ComputeProp& prop);
    
    // Generate main render function
    llvm::Function* generateRenderFunc(const GraphNode& graph);
    
    // Generate event dispatch table
    llvm::GlobalVariable* generateEventTable();
};
```

**LLVM IR Example**:

```llvm
; Mermaid graph: A --> B{value > 10}
; Compiled to:

%NodeState = type { i8*, i32, double }

define double @compute_B(i8* %node_ptr) {
entry:
    %value_ptr = getelementptr %NodeState* %node_ptr, i32 0, i32 2
    %value = load double, double* %value_ptr
    %cond = fcmp ogt double %value, 10.0
    %result = select i1 %cond, double 1.0, double 0.0
    ret double %result
}

define void @render_graph() {
entry:
    ; Render all nodes and edges
    call void @render_A()
    call void @render_B()
    ret void
}

; Event dispatch table
@event_table = constant [2 x i8*] [
    i8* @onclick_A,
    i8* @onclick_B
]
```

**Runtime Functions (to be linked)**:

```cpp
extern "C" {
    // Rendering
    void render_node(const char* id, const char* label, const char* shape);
    void render_edge(const char* from, const char* to, const char* style);
    void update_style(const char* id, const char* style);
    
    // Events
    void register_click(const char* node_id, void* handler_fn);
    void register_hover(const char* node_id, void* handler_fn);
    
    // Data
    void set_node_property(const char* id, const char* prop, double value);
    double get_node_property(const char* id, const char* prop);
    
    // Animation
    void animate_edge(const char* from, const char* to, double duration);
    void highlight_path(const char** nodes, int count);
}
```

### 3.5 Optimizer

**Purpose**: Optimize generated LLVM IR

**Location**: `src/compiler/optimizer/`

**Optimization Passes**:

| Pass | Description | Impact |
|------|-------------|--------|
| Inlining | Inline compute functions | Reduce call overhead |
| Constant Folding | Pre-compute constant expressions | Runtime speedup |
| DCE | Remove unused render code | Binary size |
| Loop Unroll | Unroll animation loops | Performance |
| Vectorize | SIMD for multiple updates | Batch operations |

**Usage**:
```bash
opt -O3 - passes input.ll -o optimized.ll
llc -O3 optimized.ll -o output.o
```

### 3.6 JIT Executor

**Purpose**: Execute LLVM IR at runtime without full compilation

**Location**: `src/runtime/jit/`

**Execution Model**:

```cpp
class DiagramJIT {
    llvm::orc::JITCompiler* jit;
    std::unordered_map<std::string, JITSymbol> functions;
    
public:
    // Load and compile IR
    void loadModule(llvm::Module* module);
    
    // Execute click handler
    double executeClick(const std::string& node_id, double* args, int argc);
    
    // Execute computed property
    double evaluate(const std::string& expr, const NodeState& state);
    
    // Hot reload
    void reloadModule(llvm::Module* new_module);
};
```

### 3.7 Event Loop & Runtime

**Purpose**: Handle user interactions and animation

**Location**: `src/runtime/event_loop/`

**Event System**:

```cpp
enum class EventType {
    CLICK, HOVER, FOCUS, BLUR,
    KEYPRESS, TIMER, DATA_UPDATE
};

struct Event {
    EventType type;
    std::string target_id;
    std::string event_data;
    timestamp_t timestamp;
};

class EventLoop {
    std::queue<Event> event_queue;
    std::unordered_map<std::string, std::vector<EventHandler>> handlers;
    
public:
    void dispatch(Event event);
    void registerHandler(const std::string& target, EventHandler handler);
    void run();  // Main event loop
};
```

### 3.8 Render Engine

**Purpose**: Draw and update diagram visualization

**Location**: `src/runtime/renderer/`

**Rendering Options**:

| Backend | Use Case | Performance |
|---------|----------|-------------|
| Canvas 2D | Simple diagrams | Fast |
| WebGL | Complex animations | Very fast |
| SVG | Scalable, accessible | Medium |
| Native (Metal/DirectX) | Desktop apps | Fastest |

**Render API**:

```cpp
class RenderEngine {
public:
    void init(const RenderConfig& config);
    void clear();
    void renderNode(const Node& node);
    void renderEdge(const Edge& edge);
    void updateStyle(const std::string& id, const Style& style);
    void animate(const Animation& anim);
    void highlight(const std::vector<std::string>& ids);
    void draw();
};
```

---

## 4. Extended Mermaid Syntax

### 4.1 Interactive Extensions

Standard Mermaid extended with execution capabilities:

```mermaid
graph TD
    A[Start] --> B{User clicks?}
    B -->|Yes| C[Compute: value * 2]
    B -->|No| D[Render: "Waiting..."]
    C --> E[Update: display = result]
    D --> E
    
    %% Event handlers
    click A trigger navigate "/home"
    click B compute "validateInput()"
    hover C highlight "pulse"
    
    %% Computed properties
    render E when "result > threshold" style "fill:#f96"
    update display interval 1000 "tick()"
```

### 4.2 New Directives

| Directive | Syntax | Description |
|-----------|--------|-------------|
| `click` | `click NODE ACTION EXPR` | Register click handler |
| `hover` | `hover NODE STYLE` | Hover style change |
| `compute` | `compute PROP EXPR` | Computed property |
| `when` | `when COND STYLE` | Conditional styling |
| `update` | `update PROP INTERVAL EXPR` | Periodic update |
| `onload` | `onload EXPR` | Execute on render |

---

## 5. Implementation Roadmap

### 5.1 Phase 1: Foundation (Months 1-2)

- [ ] Lexer implementation (C++ with Flex)
- [ ] Basic Parser (Bison)
- [ ] AST node definitions
- [ ] Simple Mermaid support: graph, node, edge
- [ ] Emit basic LLVM IR

### 5.2 Phase 2: Semantics (Months 3-4)

- [ ] Semantic analyzer
- [ ] Symbol table implementation
- [ ] Error reporting with locations
- [ ] Support all Mermaid diagram types
- [ ] Style and class definitions

### 5.3 Phase 3: Interactivity (Months 5-6)

- [ ] Click handler codegen
- [ ] Event system implementation
- [ ] Compute property support
- [ ] JIT compilation
- [ ] Basic render engine (SVG)

### 5.4 Phase 4: Polish (Months 7-8)

- [ ] Animation system
- [ ] Optimizer passes
- [ ] Hot reload capability
- [ ] Debugger integration
- [ ] Performance optimization

### 5.5 Phase 5: Ecosystem (Months 9-12)

- [ ] VS Code extension
- [ ] Web playground
- [ ] NPM package
- [ ] Documentation and examples
- [ ] Community plugins

---

## 6. File Structure

```
diagramvm/
├── src/
│   ├── compiler/
│   │   ├── lexer/
│   │   │   ├── lexer.h
│   │   │   ├── lexer.cpp
│   │   │   ├── token.h
│   │   │   └── location.h
│   │   ├── parser/
│   │   │   ├── parser.y
│   │   │   ├── parser.cpp
│   │   │   ├── ast.h
│   │   │   └── ast.cpp
│   │   ├── semantic/
│   │   │   ├── analyzer.h
│   │   │   ├── analyzer.cpp
│   │   │   ├── symbol.h
│   │   │   └── types.h
│   │   ├── codegen/
│   │   │   ├── codegen.h
│   │   │   ├── codegen.cpp
│   │   │   ├── ir_builder.h
│   │   │   └── runtime_decl.h
│   │   ├── optimizer/
│   │   │   ├── optimizer.h
│   │   │   └── passes/
│   │   └── main.cpp
│   ├── runtime/
│   │   ├── jit/
│   │   │   ├── jit.h
│   │   │   └── executor.cpp
│   │   ├── event_loop/
│   │   │   ├── event_loop.h
│   │   │   ├── handlers.cpp
│   │   │   └── dispatcher.cpp
│   │   ├── renderer/
│   │   │   ├── renderer.h
│   │   │   ├── canvas.cpp
│   │   │   ├── svg.cpp
│   │   │   └── webgl.cpp
│   │   ├── runtime.cpp
│   │   └── runtime.h
│   └── frontend/
│       ├── editor/
│       ├── preview/
│       └── debugger/
├── include/
│   └── diagramvm/
│       ├── diagramvm.h
│       └── config.h
├── tests/
│   ├── lexer/
│   ├── parser/
│   ├── semantic/
│   ├── codegen/
│   └── runtime/
├── examples/
│   ├── simple/
│   ├── interactive/
│   └── animations/
├── CMakeLists.txt
├── Dockerfile
└── README.md
```

---

## 7. Dependencies

### 7.1 Required

| Library | Version | Purpose |
|---------|---------|---------|
| LLVM | 17+ | IR, JIT, codegen |
| Clang | 17+ | C++ compilation |
| CMake | 3.20+ | Build system |

### 7.2 Optional

| Library | Version | Purpose |
|---------|---------|---------|
| Flex | 2.6+ | Lexer generation |
| Bison | 3.8+ | Parser generation |
|inja| 3.2+| Template engine|
| SDL2 | 2.0+ | Native rendering |

### 7.3 Build Commands

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build -j$(nproc)

# Install
cmake --install build
```

---

## 8. Usage Examples

### 8.1 Command Line

```bash
# Compile and run
diagramvm input.mmd -o output

# Compile to LLVM IR
diagramvm input.mmd --emit-llvm -o output.ll

# JIT execute and show
diagramvm input.mmd --jit --show

# Interactive mode
diagramvm input.mmd --interactive
```

### 8.2 Library Usage

```cpp
#include <diagramvm/diagramvm.h>

int main() {
    DiagramVM vm;
    
    // Load and compile
    vm.loadFile("flow.mmd");
    vm.compile();
    
    // Execute with callback
    vm.onClick("node_a", [](double value) {
        std::cout << "Clicked! Value: " << value << std::endl;
        return value * 2;
    });
    
    // Run event loop
    vm.run();
    
    return 0;
}
```

### 8.3 Web Usage

```html
<script src="diagramvm.js"></script>
<div id="diagram"></div>
<script>
  const diagram = DiagramVM.render(`
    graph TD
      A[Click me] --> B{Condition}
      B -->|Yes| C[Result]
  `, {
    click: {
      A: (e) => alert('Node A clicked!')
    }
  });
</script>
```

---

## 9. Integration with CSOS

### 9.1 CSOS Ring Integration

| Ring | Usage |
|------|-------|
| eco_domain | Track diagram types used |
| eco_cockpit | Monitor execution metrics |
| human.json | User preferences for editor |

### 9.2 CSOS Commands

```bash
# Compile diagram
csos-core command="diagramvm compile flow.mmd" substrate=diagram

# Run interactive
csos-core command="diagramvm interactive flow.mmd" substrate=diagram
```

---

## 10. Performance Targets

| Metric | Target | Measurement |
|--------|--------|--------------|
| Compilation Speed | < 100ms | Per diagram |
| Render FPS | 60 fps | Animation |
| Memory Usage | < 100MB | Base runtime |
| Cold Start | < 500ms | JIT warmup |
| Interaction Latency | < 16ms | Click to response |

---

## Appendix A: Grammar Specification

```
graph           ::= DIRECTIVE? GRAPH_TYPE ID? DIRECTION statement_list
statement_list ::= statement+
statement       ::= node_stmt | edge_stmt | style_stmt | class_stmt | directive

node_stmt       ::= ID (LABEL)? (SHAPE)?
edge_stmt       ::= edge END edge_stmt*
edge            ::= ID EDGE_OP ID (LABEL)?

style_stmt      ::= STYLE ID (COMMA ID)* style_list
class_stmt      ::= CLASS ID style_list

directive       ::= CLICK | HOVER | COMPUTE | WHEN | UPDATE

expression      ::= identifier | number | string | binary_expr | function_call
binary_expr     ::= expression (PLUS|MINUS|MUL|DIV) expression
function_call   ::= ID LPAREN expression_list RPAREN
```

---

*Document Version: 1.0*
*DiagramVM Compiler System*
