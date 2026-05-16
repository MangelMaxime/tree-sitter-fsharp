# tree-sitter-fsharp — Grammar Specification

> **Goal:** A Tree-sitter grammar for F# that produces a stable, consistent parse tree
> across real-world codebases. Consistent colours over fancy colours. No file-wide
> cascade on a single unsupported construct.

---

## Repository layout

```
tree-sitter-fsharp/
├── grammar.js                  # grammar definition (source of truth)
├── package.json
├── binding.gyp
├── src/
│   ├── parser.c                # generated — do not edit
│   ├── tree_sitter/
│   │   └── parser.h
│   └── scanner.c               # hand-written external scanner (indentation)
├── queries/
│   ├── highlights.scm          # syntax highlighting
│   ├── locals.scm              # scope / variable tracking
│   ├── textobjects.scm         # function / class text objects
│   └── indents.scm             # auto-indent rules
├── test/
│   └── corpus/
│       ├── phase1_lexer.txt
│       ├── phase2_patterns.txt
│       ├── phase3_types.txt
│       ├── phase4_modules.txt
│       ├── phase5_oop.txt
│       ├── phase6_ce.txt
│       └── phase7_advanced.txt
└── Makefile
```

---

## Non-negotiable constraints

1. **Error locality.** A parse error in one function must not produce `ERROR` nodes
   outside that function's subtree. Every top-level rule must have a recovery path.
2. **No file-wide ERROR nodes.** If the parser cannot handle a construct, it should
   produce a named `_unknown` placeholder node, not swallow the rest of the file.
3. **Incremental re-parse must be stable.** Adding a line in the middle of a file
   must not re-highlight lines below the edit point that were already correct.
4. **Phases are additive.** Completing phase N must not regress any test from phases
   1 through N-1. Run the full corpus before declaring a phase done.
5. **No type-level disambiguation.** Tree-sitter is context-free. Anywhere the F#
   spec resolves ambiguity via type inference, the grammar must pick the most
   conservative parse and leave refinement to the highlights query.

---

## External scanner (scanner.c)

F# uses significant indentation. The external scanner is responsible for injecting
synthetic `INDENT` / `DEDENT` / `NEWLINE` tokens into the token stream, similar to
the approach used by tree-sitter-python.

### Tokens emitted by the scanner

| Token        | When emitted                                              |
|--------------|-----------------------------------------------------------|
| `NEWLINE`    | End of a logical line (not inside brackets/parens)        |
| `INDENT`     | Column increases relative to current indent level         |
| `DEDENT`     | Column decreases; may emit multiple DEDENTs in one step   |
| `THEN`       | Contextual — inside `if` when `then` starts a new line   |

### Indentation rules (from the F# spec §15)

- Inside `(`, `[`, `[|`, `{`, `{|` — indentation is ignored (offside rule suspended)
- `let`, `type`, `module`, `namespace` — start a new offside context
- `match … with` arms — each `|` must be at the same column as the first `|`
- `if … then … else` — `else` must be at or left of the column of `if`
- Continuation lines — a line is a continuation if its first token is an operator
  or if the previous line ended with an infix operator

### Scanner state

```c
typedef struct {
  uint32_t indent_stack[256];  // stack of indent levels
  uint32_t stack_top;
  bool     at_start_of_line;
} Scanner;
```

---

## Phase 1 — Lexer & expression spine

### Keywords

```js
// grammar.js — keyword list
const KEYWORDS = [
  'let', 'in', 'and', 'rec',
  'if', 'then', 'else',
  'match', 'with', 'function',
  'fun', 'do', 'done',
  'for', 'while', 'to', 'downto',
  'try', 'with', 'finally', 'raise', 'reraise',
  'type', 'of', 'inherit',
  'mutable', 'lazy',
  'open', 'module', 'namespace',
  'true', 'false', 'null',
  'new', 'struct', 'class', 'interface', 'end',
  'abstract', 'default', 'override', 'virtual',
  'static', 'member', 'val',
  'internal', 'private', 'public',
  'extern', 'void', 'return',
  'upcast', 'downcast',
  'begin', 'end',
  'sig', 'global',
  'inline', 'when', 'as', 'base', 'assert',
  'fixed',
];
```

### Literals

| Node name              | Pattern / notes                                         |
|------------------------|---------------------------------------------------------|
| `int_literal`          | `[0-9][0-9_]*[ysuln]?` plus `0x`, `0o`, `0b` prefixes |
| `float_literal`        | `[0-9][0-9_]*\.[0-9]*([eE][+-]?[0-9]+)?[fFmM]?`      |
| `char_literal`         | `'.'` with standard escape sequences                   |
| `string_literal`       | `"…"` with escape sequences                            |
| `verbatim_string`      | `@"…"` — backslash is not an escape                    |
| `triple_quoted_string` | `"""…"""` — no escapes at all                          |
| `bool_literal`         | `true` / `false`                                       |
| `unit`                 | `()`                                                   |
| `null_literal`         | `null`                                                 |

### Expression hierarchy (precedence, low → high)

```
expression
  ├── let_expression          (let x = … in …)
  ├── sequential_expression   (e1; e2)
  ├── if_expression
  ├── match_expression
  ├── try_expression
  ├── lambda_expression       (fun … -> …)
  ├── tuple_expression        (e1, e2, …)        right-assoc
  ├── or_expression           (||)               left-assoc
  ├── and_expression          (&&)               left-assoc
  ├── comparison_expression   (= <> < > <= >=)   non-assoc
  ├── cons_expression         (::)               right-assoc
  ├── append_expression       (@)                right-assoc
  ├── additive_expression     (+ -)              left-assoc
  ├── multiplicative_expression (* / %)          left-assoc
  ├── power_expression        (**)               right-assoc
  ├── pipe_right_expression   (|>)               left-assoc
  ├── pipe_left_expression    (<|)               right-assoc
  ├── compose_right           (>>)               left-assoc
  ├── compose_left            (<<)               right-assoc
  ├── unary_expression        (not - + ~~~ ~~~)
  ├── application_expression  (f x)              left-assoc
  ├── dot_expression          (x.y)
  ├── index_expression        (x.[i] / x[i])
  └── atomic_expression
        ├── identifier
        ├── literal
        ├── parenthesized     (expr)
        ├── list_literal      [e1; e2]
        ├── array_literal     [| e1; e2 |]
        ├── record_literal    { f1 = e1 }
        └── unit              ()
```

### Operators (built-in and custom)

```
// Built-in infix operators — the grammar explicitly names these
infix_op :=
  '|>' | '<|' | '>>' | '<<'
  | '||' | '&&'
  | '=' | '<>' | '<' | '>' | '<=' | '>='
  | '+' | '-' | '*' | '/' | '%' | '**'
  | '::' | '@'
  | ':=' | '<-'

// Custom operators — any symbol sequence not already claimed
custom_op := /[!%&*+\-./<=>?@^|~][!%&*+\-./<=>?@^|~:]*/
```

---

## Phase 2 — Patterns & match expressions

### Pattern grammar

```
pattern :=
  '_'                                     -- wildcard
  | literal                               -- constant
  | long_identifier                       -- variable or union case
  | long_identifier pattern               -- union case with payload
  | pattern 'as' identifier               -- as-pattern
  | pattern '|' pattern                   -- or-pattern
  | pattern '&' pattern                   -- and-pattern
  | pattern '::' pattern                  -- cons-pattern
  | '(' pattern ')'                       -- parenthesised
  | '(' pattern ':' type ')'              -- type-annotated
  | '(' pattern ',' pattern (',' pattern)* ')'  -- tuple
  | '[' (pattern (';' pattern)*)? ']'    -- list
  | '[|' (pattern (';' pattern)*)? '|]'  -- array
  | '{' field_pattern (';' field_pattern)* '}'  -- record
  | ':?' atomic_type ('as' identifier)?  -- type test
  | 'null'                               -- null
  | 'struct' '(' pattern ',' pattern ')' -- struct tuple

field_pattern := long_identifier '=' pattern
```

### Match expression

```
match_expression :=
  'match' expression 'with'
  match_arm+

match_arm :=
  '|' pattern ('when' expression)? '->' expression
```

### Disambiguation rule

A bare `Identifier` in a pattern position must be classified as:
- **variable** if it starts with a lowercase letter
- **union case** if it starts with an uppercase letter
- **long identifier** (module-qualified) if it contains `.`

The grammar cannot know if `Foo` is a DU case or a mistyped variable — emit
`long_identifier` and let the highlight query decide the colour.

---

## Phase 3 — Type definitions

### Type annotations (inline)

```
type_annotation := ':' type_expression

type_expression :=
  long_identifier type_args?              -- named type
  | type_expression '->' type_expression -- function type (right-assoc)
  | type_expression '*' type_expression  -- tuple type
  | type_expression list                 -- postfix list
  | type_expression []                   -- postfix array
  | type_expression option               -- postfix option
  | '\'' identifier                      -- type parameter
  | '^' identifier                       -- statically resolved type param
  | '(' type_expression ')'
  | '_'                                  -- anonymous / inferred

type_args := '<' type_expression (',' type_expression)* '>'
```

### Type definitions

```
type_definition :=
  'type' attributes? access? identifier type_params?
  '=' type_definition_body

type_definition_body :=
  record_definition
  | union_definition
  | alias_definition
  | enum_definition
  | class_definition
  | struct_definition
  | interface_definition
  | abbreviation_definition

record_definition :=
  '{' record_field (';' record_field)* '}'

record_field :=
  attributes? 'mutable'? identifier ':' type_expression

union_definition :=
  union_case ('|' union_case)*

union_case :=
  '|'? identifier ('of' union_case_fields)?

union_case_fields :=
  type_expression                         -- single unnamed field
  | named_field ('+' named_field)*       -- named fields

named_field := identifier ':' type_expression

enum_definition :=
  '|'? enum_case ('|' enum_case)*

enum_case := identifier '=' literal

type_params :=
  '\'' identifier
  | '<' type_param (',' type_param)* '>'

type_param := attributes? ('\''|'^') identifier
```

### Recursive type groups

```
recursive_type_group :=
  type_definition ('and' type_definition)*
```

---

## Phase 4 — Modules, namespaces & attributes

```
source_file :=
  namespace_declaration?
  (module_declaration | top_level_binding | open_statement | attribute_set)*

namespace_declaration :=
  'namespace' ('global' | long_identifier)

module_declaration :=
  attributes? access? 'module' ('rec')? long_identifier
  ('=' INDENT module_body DEDENT)?

module_body :=
  (open_statement | module_declaration | type_definition
  | let_binding | do_binding | val_declaration | attribute_set)*

open_statement :=
  'open' long_identifier

module_abbreviation :=
  'module' identifier '=' long_identifier

attribute_set :=
  '[<' attribute (',' attribute)* '>]'

attribute :=
  long_identifier ('(' argument_list ')')?
```

### Access modifiers

`public` | `private` | `internal` — may appear before `let`, `type`, `module`,
`member`, `val`.

---

## Phase 5 — OOP & class members

```
class_definition :=
  primary_constructor?
  INDENT class_member* DEDENT

primary_constructor :=
  access? '(' constructor_param (',' constructor_param)* ')'

constructor_param :=
  '?'? identifier (':' type_expression)?

class_member :=
  let_binding
  | do_binding
  | member_definition
  | abstract_member
  | val_field
  | type_definition
  | interface_impl

member_definition :=
  attributes? ('static')? 'member' access?
  self_identifier '.' identifier
  function_params_or_value
  (':' type_expression)?
  '=' expression

self_identifier := identifier | '_'

abstract_member :=
  attributes? 'abstract' ('member')? access?
  identifier ':' type_expression

val_field :=
  attributes? 'val' 'mutable'? access? identifier ':' type_expression
  ('=' expression)?

interface_impl :=
  'interface' type_expression 'with'
  INDENT member_definition+ DEDENT

object_expression :=
  '{' 'new' type_expression ('(' argument_list ')')? 'with'
  INDENT member_definition+ DEDENT '}'
```

---

## Phase 6 — Computation expressions

### Strategy

The grammar cannot validate which CE keywords are legal in which builder.
Parse all CE bodies uniformly — treat `let!`, `do!`, `yield!`, `return!`,
`match!`, `use!` as contextual keywords valid inside any `{ }` block that
follows an expression (the builder).

```
computation_expression :=
  expression '{' INDENT ce_statement* DEDENT '}'

ce_statement :=
  'let' '!'? pattern '=' expression NEWLINE ce_statement*   -- let / let!
  | 'do' '!'? expression NEWLINE                            -- do / do!
  | 'return' '!'? expression                                -- return / return!
  | 'yield' '!'? expression                                 -- yield / yield!
  | 'use' '!'? pattern '=' expression NEWLINE ce_statement* -- use / use!
  | 'match' '!'? expression 'with' match_arm+               -- match / match!
  | 'for' pattern 'in' expression 'do' INDENT ce_statement* DEDENT
  | 'while' expression 'do' INDENT ce_statement* DEDENT
  | 'if' expression 'then' INDENT ce_statement* DEDENT
    ('else' INDENT ce_statement* DEDENT)?
  | expression NEWLINE                                       -- bare expression
```

### Sequence / list / array comprehensions

```
seq_expression :=
  'seq' '{' INDENT ce_statement* DEDENT '}'

list_comprehension :=
  '[' INDENT ce_statement* DEDENT ']'

array_comprehension :=
  '[|' INDENT ce_statement* DEDENT '|]'
```

---

## Phase 7 — Advanced & niche features

### Active patterns

```
active_pattern_definition :=
  'let' ('inline')? '(' active_pattern_case+ '|'? ')'
  ('_' | pattern)* '=' expression

active_pattern_case :=
  '|' identifier           -- total case
  | '|' '_'                -- catch-all (partial active pattern)
```

### String interpolation

```
interpolated_string :=
  '$"' interpolated_part* '"'
  | '$"""' interpolated_part* '"""'

interpolated_part :=
  string_chars
  | '{' expression '}'
  | '{{' | '}}'            -- escaped braces
```

### Units of measure

```
measure_expression :=
  measure_atom
  | measure_expression '*' measure_expression
  | measure_expression '/' measure_expression
  | measure_expression '^' int_literal
  | '1'                                   -- dimensionless

measure_atom :=
  long_identifier
  | '(' measure_expression ')'
  | '_'

measure_annotated_literal :=
  float_literal '<' measure_expression '>'
  | int_literal '<' measure_expression '>'
```

### Exception definitions

```
exception_definition :=
  attributes? 'exception' identifier ('of' union_case_fields)?

try_expression :=
  'try' INDENT expression DEDENT
  (with_clause | finally_clause | with_clause finally_clause)

with_clause :=
  'with' match_arm+

finally_clause :=
  'finally' INDENT expression DEDENT
```

### Quotations

```
typed_quotation :=
  '<@' expression '@>'

untyped_quotation :=
  '<@@' expression '@@>'

quotation_splice :=
  '%' expression             -- typed splice
  | '%%' expression          -- untyped splice
```

---

## highlights.scm — capture names

The following captures are the only ones to assign initially.
Resist adding more until each category is stable.

```scheme
; Phase 1 — basics
(keyword) @keyword
(int_literal) @number
(float_literal) @number.float
(char_literal) @character
(string_literal) @string
(verbatim_string) @string
(triple_quoted_string) @string
(bool_literal) @boolean
(unit) @constant.builtin
(null_literal) @constant.builtin
(line_comment) @comment
(block_comment) @comment
(xml_doc_comment) @comment.documentation

; Phase 1 — operators
(infix_op) @operator
(custom_op) @operator

; Phase 1 — identifiers (conservative — everything is variable until proven otherwise)
(identifier) @variable

; Phase 2 — patterns
(wildcard_pattern) @variable.special    ; _
(long_identifier) @constructor          ; uppercase = union case (heuristic)

; Phase 3 — types
(type_name) @type
(type_parameter) @type.parameter        ; 'a ^T
(record_field_name) @property
(union_case_name) @constructor

; Phase 4 — modules
(module_name) @module
(namespace_name) @namespace
(open_statement (long_identifier) @namespace)
(attribute_name) @attribute

; Phase 5 — members
(self_identifier) @variable.builtin
(member_name) @function.method

; Phase 6 — CE keywords (contextual)
(ce_let_bang) @keyword
(ce_do_bang) @keyword
(ce_yield_bang) @keyword
(ce_return_bang) @keyword
(ce_use_bang) @keyword
(ce_match_bang) @keyword

; Phase 7
(active_pattern_case) @function.special
(measure_unit) @type.special
(quotation) @string.special
```

---

## locals.scm — scope tracking

```scheme
; Scopes
(let_binding) @local.scope
(lambda_expression) @local.scope
(match_arm) @local.scope
(class_definition) @local.scope
(module_declaration) @local.scope

; Definitions
(let_binding (identifier) @local.definition)
(lambda_expression (identifier) @local.definition)
(match_arm (identifier) @local.definition)   ; pattern variables
(record_field_name) @local.definition

; References
(identifier) @local.reference
```

---

## textobjects.scm

```scheme
; Function / value binding
(let_binding) @function.outer
(let_binding body: (_) @function.inner)

; Type definition
(type_definition) @class.outer
(type_definition body: (_) @class.inner)

; Match arm
(match_arm) @conditional.outer
(match_arm consequence: (_) @conditional.inner)

; Call expression
(application_expression) @call.outer
(application_expression argument: (_) @call.inner)
```

---

## indents.scm

```scheme
; Increase indent after these
[
  (let_binding)
  (if_expression)
  (match_expression)
  (class_definition)
  (module_declaration)
  (computation_expression)
  (try_expression)
  (for_expression)
  (while_expression)
] @indent

; Decrease on these
[
  "in"
  "then"
  "else"
  "with"
  "finally"
] @outdent

; Don't indent inside strings
(string_literal) @auto
```

---

## Corpus test format

Each corpus file follows tree-sitter's test format:

```
================================================================================
Let binding — simple value
================================================================================

let x = 42

--------------------------------------------------------------------------------

(source_file
  (let_binding
    (identifier)
    (int_literal)))

================================================================================
Let binding — function with type annotation
================================================================================

let f (x: int) : int = x + 1

--------------------------------------------------------------------------------

(source_file
  (let_binding
    (identifier)
    (parameter
      (identifier)
      (type_annotation
        (type_name)))
    (type_annotation
      (type_name))
    (application_expression
      ...)))
```

### Minimum corpus size per phase before marking it done

| Phase | Minimum tests |
|-------|--------------|
| 1     | 30           |
| 2     | 40           |
| 3     | 30           |
| 4     | 20           |
| 5     | 30           |
| 6     | 25           |
| 7     | 20 per feature |

---

## Makefile

```makefile
.PHONY: generate build test watch clean

generate:
	tree-sitter generate

build: generate
	tree-sitter build

test: build
	tree-sitter test

# Run only one phase corpus
test-phase-%: build
	tree-sitter test --include "phase$*"

watch:
	watchexec -e js,c "make test"

# Parse a real file and show the tree
parse:
	tree-sitter parse $(FILE)

# Open the playground in the browser (requires wasm build)
playground:
	tree-sitter build --wasm && tree-sitter playground

clean:
	rm -rf src/parser.c node_modules
```

---

## package.json

```json
{
  "name": "tree-sitter-fsharp",
  "version": "0.1.0",
  "description": "Tree-sitter grammar for F#",
  "main": "bindings/node",
  "scripts": {
    "build": "node-gyp rebuild",
    "test": "tree-sitter test"
  },
  "dependencies": {
    "node-gyp": "^10.0.0"
  },
  "devDependencies": {
    "tree-sitter-cli": "^0.22.0"
  },
  "tree-sitter": [
    {
      "scope": "source.fsharp",
      "file-types": ["fs", "fsi", "fsx"],
      "highlights": "queries/highlights.scm",
      "locals": "queries/locals.scm",
      "textObjects": "queries/textobjects.scm"
    }
  ]
}
```

---

## Helix languages.toml (local dev config)

```toml
# ~/.config/helix/languages.toml

[[language]]
name = "fsharp"
scope = "source.fsharp"
file-types = ["fs", "fsi", "fsx"]
comment-token = "//"
indent = { tab-width = 4, unit = "    " }
roots = [".git", "*.fsproj", "*.sln"]

[language.grammar]
name = "fsharp"
source = { path = "/absolute/path/to/tree-sitter-fsharp" }
```

Run `hx --grammar build` after every `tree-sitter generate`.

---

## Decision log

Record design decisions here as you make them. Avoids re-arguing the same
points later.

| # | Decision | Rationale |
|---|----------|-----------|
| 1 | External scanner for INDENT/DEDENT | F# indentation rules cannot be expressed in a context-free grammar |
| 2 | CE keywords are contextual, not reserved | Prevents breaking non-CE code that uses `yield` / `return` as identifiers in custom operators |
| 3 | Bare `Identifier` defaults to `@variable` | Prevents false positives; upgrade to `@constructor` / `@type` in highlights.scm heuristically |
| 4 | Units of measure deferred to phase 7 | Syntactically rare; disambiguating `<kg>` from generic args requires lookahead that breaks the expression spine |
| 5 | Quotations deferred to phase 7 | Very rare in application code; `<@` conflicts with less-than + at-symbol in custom operators |
| 6 | Object expressions flagged high-risk | `{ new T with ... }` collides with record literal syntax; needs careful precedence in the scanner |
