# F# Tree-sitter Grammar — Phase 1 Implementation Plan

## Goal

Build the lexer and expression spine of an F# tree-sitter grammar, one validated step at a time. Each step produces a working grammar that passes `tree-sitter generate` and a small targeted corpus test. No step should introduce more than ~50 lines of grammar.js change.

---

## Validation Checkpoint (after EVERY step)

```bash
cd /home/mmangel/Workspaces/Github/MangelMaxime/tree-sitter-fsharp-helix/main

# 1. Grammar generates without errors
tree-sitter generate

# 2. Existing corpus tests still pass
tree-sitter test

# 3. New corpus test for this step passes
tree-sitter test --include "phase1"

# 4. Copy to Helix and check health
./dev.sh
helix --health fsharp
```

---

## Step 0 — Project scaffolding

**What:** Create directories and placeholder files so every subsequent step has somewhere to land.

**Files to create:**
- `test/corpus/phase1_lexer.txt` — empty file with one passing dummy test
- `queries/highlights.scm` — keep existing `["open"] @keyword`
- `queries/injections.scm`, `queries/locals.scm`, `queries/textobjects.scm`, `queries/indents.scm`, `queries/tags.scm`, `queries/rainbows.scm` — empty (prevents Helix system fallback)

**Validation:** `tree-sitter test` passes (dummy test only).

---

## Step 1 — Identifier and long_identifier

**What:** Define the atomic name tokens. F# identifiers can contain apostrophes (`x'`, `x''`).

**grammar.js additions:**
```js
identifier: _ => /[a-zA-Z_][a-zA-Z0-9_']*/,

long_identifier: $ =>
  prec.right(
    seq($.identifier, repeat(seq('.', $.identifier)))
  ),
// Note: repeat (not repeat1) — a bare identifier is also a long_identifier with 0 dots.
// This means _expression only needs long_identifier, never bare identifier.
// Having both in _expression would create reduce/reduce conflicts in application_expression:
// "f x" could parse as app(identifier, identifier) or app(long_identifier, long_identifier).
```

**Why `word: $ => $.identifier` matters:** tree-sitter uses the `word` token to avoid parsing keywords at identifier positions. But because we declare keywords as literal strings (e.g. `"let"`), tree-sitter will prefer matching the keyword over the identifier regex when the literal matches.

**Corpus test:**
- `let x = 1` — parses as `source_file` containing `identifier` "x"
- `System.Console` — parses as `long_identifier` containing two `identifier`s
- `x'` — parses as single `identifier` (not `x` followed by `'`)

**Validation:** `tree-sitter generate` succeeds; corpus test passes.

---

## Step 2 — Keywords as token rules

**What:** Add the keyword list from the SPEC. In tree-sitter, keywords are typically declared as individual token rules or literal strings in seq/choice. For F#, we keep them as inline strings in the rules that use them, NOT as a centralized `keyword` node — because we want the parser to distinguish `let` in a binding position from `let` as an identifier (which requires backticks in real F#, but we can ignore that edge case for Phase 1).

**Decision:** Do NOT create a single `keyword` rule that matches any keyword. Instead, use literal strings like `"let"`, `"if"`, etc. directly in the rules that need them. This avoids the ambiguity of a generic `keyword` node.

**Validation:** No new tests needed — this step just sets up the token vocabulary. Verify `tree-sitter generate` still passes.

---

## Step 3 — Literals (int, float, char, string)

**What:** Add all literal types from the SPEC.

**grammar.js additions (one at a time, with tests):**

### 3a — int_literal
```js
int_literal: _ => token(
  choice(
    seq(choice('0x', '0X'), /[0-9a-fA-F_]+/),
    seq(choice('0o', '0O'), /[0-7_]+/),
    seq(choice('0b', '0B'), /[01_]+/),
    /[0-9][0-9_]*/,
  )
),
```

**Corpus tests:** `42`, `0xFF`, `0o77`, `0b1010`

### 3b — float_literal
```js
float_literal: _ => token(
  choice(
    seq(/[0-9][0-9_]*/, '.', /[0-9_]*/, optional(seq(/[eE]/, optional(/[+-]/), /[0-9]+/))),
    seq(/[0-9]+/, /[eE]/, optional(/[+-]/), /[0-9]+/),
  )
),
```

**Corpus tests:** `3.14`, `1e10`, `2.5e-3`

### 3c — char_literal
```js
char_literal: _ => token(
  seq("'",
    choice(
      /[^'\\]/,
      seq('\\', choice(/./, /x[0-9a-fA-F]{2}/, /u[0-9a-fA-F]{4}/, /U[0-9a-fA-F]{8}/)),
    ),
    "'",
  )
),
```

**Corpus tests:** `'a'`, `'\\n'`, `'\\x41'`

### 3d — string_literal
```js
string_literal: _ => token(
  seq('"',
    repeat(choice(
      /[^"\\]/,
      seq('\\', choice(/./, /x[0-9a-fA-F]{2}/, /u[0-9a-fA-F]{4}/, /U[0-9a-fA-F]{8}/)),
    )),
    '"',
  )
),
```

**Corpus tests:** `"hello"`, `"line\\nbreak"`

### 3e — verbatim_string
```js
verbatim_string: _ => token(
  seq('@"', repeat(choice(/[^"]/, '""')), '"')
),
```

**Corpus tests:** `@"C:\\Users\\foo"`

### 3f — triple_quoted_string
```js
triple_quoted_string: _ => token(
  seq('"""', repeat(choice(/[^"]/, /"[^"]/, /""[^"]/)), '"""')
),
```

**Corpus tests:** `"""multi\nline"""`

### 3g — bool_literal, unit, null_literal
```js
bool_literal: _ => choice('true', 'false'),
unit: _ => seq('(', ')'),
null_literal: _ => 'null',
```

**Corpus tests:** `true`, `false`, `()`, `null`

**Validation:** Each sub-step generates and passes its own corpus test.

---

## Step 4 — Comments

**What:** Add line and block comments. Block comments in F# can be nested `(* (* inner *) *)`.

**grammar.js additions:**
```js
line_comment: _ => token(seq('//', /.*/)),

block_comment: $ => seq(
  '(*',
  repeat(choice(
    /[^(*]/,
    /[(][^*]/,
    /[*][^)]/,
    $.block_comment,
  )),
  '*)
',
),
```

**IMPORTANT:** Add both to `extras`:
```js
extras: $ => [/[\s\r\n]+/, $.line_comment, $.block_comment],
```

**Corpus tests:**
- `// line comment`
- `(* block comment *)`
- `(* nested (* inner *) *)`

**Validation:** Comments parse but do NOT appear as nodes in the tree (they are extras).

---

## Step 5 — The expression precedence constant table

**What:** Before writing any expression rules, establish the precedence levels as a constants object. This is the foundation of the entire expression hierarchy.

**grammar.js additions:**
```js
const PREC = {
  let: 1,
  sequential: 1,
  if: 2,
  match: 2,
  try: 2,
  lambda: 3,
  tuple: 4,
  compose: 5,
  pipe: 6,
  or: 7,
  and: 8,
  comparison: 9,
  cons: 10,
  append: 11,
  additive: 12,
  multiplicative: 13,
  power: 14,
  unary: 15,
  application: 16,
  dot: 17,
  index: 18,
};
```

**Precedence order (lowest → highest):**
1. let / sequential
2. if / match / try
3. lambda
4. tuple
5. compose (>>, <<)
6. pipe (|>, <|)
7. or (||)
8. and (&&)
9. comparison (=, <>, <, >, <=, >=)
10. cons (::)
11. append (@)
12. additive (+, -)
13. multiplicative (*, /, %)
14. power (**)
15. unary (not, -, +, ~~~)
16. application (f x)
17. dot (x.y)
18. index (x.[i])

**Validation:** No tests yet — this is just constants. Verify `tree-sitter generate` still passes.

---

## Step 6 — Atomic expressions

**What:** Create the `_atomic_expression` rule that serves as the leaf nodes of the expression tree.

**grammar.js additions:**
```js
_atomic_expression: $ => choice(
  $.identifier,
  $.long_identifier,
  $.int_literal,
  $.float_literal,
  $.char_literal,
  $.string_literal,
  $.verbatim_string,
  $.triple_quoted_string,
  $.bool_literal,
  $.unit,
  $.null_literal,
),
```

**Why the underscore prefix?** `_atomic_expression` is a hidden rule — it won't appear as a named node in the parse tree. The tree will show the chosen alternative (e.g., `int_literal`) directly.

**Corpus tests:** Each literal type as a standalone expression.

**Validation:** `tree-sitter parse` on a single literal produces the correct node.

---

## Step 7 — Parenthesized, list, array, record literals

**What:** Add container literals that wrap expressions.

**grammar.js additions (one at a time):**

### 7a — parenthesized_expression
```js
parenthesized_expression: $ => seq('(', $.expression, ')'),
```

**Test:** `(42)` → `parenthesized_expression(int_literal)`

### 7b — list_literal
```js
list_literal: $ => seq(
  '[',
  optional(seq($.expression, repeat(seq(';', $.expression)))),
  ']',
),
```

**Test:** `[1; 2; 3]`

### 7c — array_literal
```js
array_literal: $ => seq(
  '[|',
  optional(seq($.expression, repeat(seq(';', $.expression)))),
  '|]',
),
```

**Test:** `[| 1; 2 |]`

### 7d — record_literal
```js
record_literal: $ => seq(
  '{',
  $.field_initializer,
  repeat(seq(';', $.field_initializer)),
  '}',
),

field_initializer: $ => seq($.identifier, '=', $.expression),
```

**Test:** `{ x = 1; y = 2 }`

**Validation:** Each generates and passes tests.

---

## Step 8 — Add _atomic_expression to expression rule

**What:** Wire `_atomic_expression` into the `expression` rule so we have a working expression leaf.

**grammar.js:**
```js
expression: $ => choice(
  // ... future expression types ...
  $._atomic_expression,
),
```

**Validation:** `let x = 42` parses as a binding whose RHS is `int_literal`.

---

## Step 9 — Binary operators (low-precedence group)

**What:** Add `or_expression`, `and_expression`, `comparison_expression`, `cons_expression`, `append_expression`.

**grammar.js additions:**
```js
or_expression: $ => prec.left(PREC.or, seq($.expression, '||', $.expression)),
and_expression: $ => prec.left(PREC.and, seq($.expression, '&&', $.expression)),
comparison_expression: $ => prec.left(PREC.comparison, seq($.expression, choice('=', '<>', '<', '>', '<=', '>='), $.expression)),
cons_expression: $ => prec.right(PREC.cons, seq($.expression, '::', $.expression)),
append_expression: $ => prec.right(PREC.append, seq($.expression, '@', $.expression)),
```

**Add to `expression` rule:** include these new alternatives.

**Corpus tests:**
- `a || b && c` → `or(a, and(b, c))`
- `a :: b :: c` → `cons(a, cons(b, c))` (right-assoc)
- `a @ b` → `append(a, b)`
- `a = b` → `comparison(a, =, b)`

**Validation:** Tests verify precedence and associativity.

---

## Step 10 — Binary operators (arithmetic and pipes)

**What:** Add `additive_expression`, `multiplicative_expression`, `power_expression`, `pipe_expression`, `compose_expression`.

**grammar.js additions:**
```js
additive_expression: $ => prec.left(PREC.additive, seq($.expression, choice('+', '-'), $.expression)),
multiplicative_expression: $ => prec.left(PREC.multiplicative, seq($.expression, choice('*', '/', '%'), $.expression)),
power_expression: $ => prec.right(PREC.power, seq($.expression, '**', $.expression)),
pipe_expression: $ => prec.left(PREC.pipe, seq($.expression, choice('|>', '<|'), $.expression)),
compose_expression: $ => prec.left(PREC.compose, seq($.expression, choice('>>', '<<'), $.expression)),
```

**Add to `expression` rule.**

**Corpus tests:**
- `a + b * c` → `add(a, mul(b, c))`
- `a ** b ** c` → `pow(a, pow(b, c))` (right-assoc)
- `a + b |> c` → `pipe(add(a, b), c)`
- `f >> g >> h` → `compose(f, compose(g, h))`

**Validation:** Precedence tests pass.

---

## Step 11 — Unary expression and function application

**What:** Add `unary_expression` and `application_expression`. These are high-precedence.

**grammar.js additions:**
```js
unary_expression: $ => prec.right(PREC.unary, seq(choice('not', '-', '+', '~~~'), $.expression)),

application_expression: $ => prec.left(PREC.application, seq($.expression, $.expression)),
```

**Add to `expression` rule.**

**Corpus tests:**
- `not x` → `unary(not, x)`
- `-42` → `unary(-, 42)`
- `f x` → `app(f, x)`
- `f x y` → `app(app(f, x), y)` (left-assoc, curried)
- `f x + y` → `add(app(f, x), y)` (application binds tighter than additive)

**Validation:** Application precedence works correctly.

---

## Step 12 — Dot and index expressions

**What:** Add `dot_expression` and `index_expression`. These are the highest-precedence expressions.

**grammar.js additions:**
```js
dot_expression: $ => prec.left(PREC.dot, seq($.expression, '.', choice($.identifier, $.long_identifier))),

index_expression: $ => prec.left(PREC.index, seq($.expression, '.', '[', $.expression, ']')),
```

**Add to `expression` rule.**

**Corpus tests:**
- `System.Console` → `dot(System, Console)`
- `arr.[0]` → `index(arr, 0)`
- `f x.y` → `app(f, dot(x, y))` (dot binds tighter than application)

**Validation:** Dot/index bind tighter than application.

---

## Step 13 — Complex expressions (if, match, lambda, sequential, tuple)

**What:** Add control flow and grouping expressions.

**grammar.js additions:**
```js
if_expression: $ => prec.right(PREC.if, seq('if', $.expression, 'then', $.expression, optional(seq('else', $.expression)))),

match_expression: $ => prec.right(PREC.match, seq('match', $.expression, 'with', repeat1($.match_arm))),

lambda_expression: $ => prec.right(PREC.lambda, seq('fun', repeat1($._simple_pattern), '->', $.expression)),

sequential_expression: $ => prec.right(PREC.sequential, seq($.expression, ';', $.expression)),

tuple_expression: $ => prec.right(PREC.tuple, seq($.expression, ',', $.expression, repeat(seq(',', $.expression)))),
```

**Add to `expression` rule.**

**Corpus tests:**
- `if x then 1 else 2`
- `match x with | _ -> 0`
- `fun x -> x + 1`
- `printfn "a"; 42`
- `(1, 2, 3)` → `tuple(1, 2, 3)`

**Validation:** Each construct parses correctly.

---

## Step 14 — Let expression and let binding

**What:** Add `let_expression` (inline `let ... in ...`) and `let_binding` (top-level).

**grammar.js additions:**
```js
let_expression: $ => prec.right(PREC.let, seq(
  'let',
  optional('rec'),
  $._binding_head,
  optional(seq(':', $.type_expression)),
  '=',
  $.expression,
  'in',
  $.expression,
)),

let_binding: $ => seq(
  'let',
  optional('rec'),
  optional($.access_modifier),
  $._binding_head,
  optional(seq(':', $.type_expression)),
  '=',
  $.expression,
),

_binding_head: $ => choice(
  seq($.identifier, repeat($._simple_pattern)), // function
  $.pattern, // value
),

do_expression: $ => seq('do', $.expression),

access_modifier: _ => choice('public', 'private', 'internal'),
```

**Add to `expression` and `_top_level_item` rules.**

**Corpus tests:**
- `let x = 1` (top-level binding)
- `let x = 1 in x + 1` (inline let)
- `let rec fib n = ...` (recursive)
- `let add a b = a + b` (function)

**Validation:** `let` at top-level is a binding; `let` in expression position requires `in`.

---

## Step 15 — Patterns (basic)

**What:** Add pattern rules for function parameters, match arms, and let bindings.

**grammar.js additions:**
```js
pattern: $ => choice(
  $.wildcard_pattern,
  $.literal_pattern,
  $.identifier_pattern,
  $.parenthesized_pattern,
  $.tuple_pattern,
  $.list_pattern,
  $.array_pattern,
  $.record_pattern,
  $.cons_pattern,
  $.as_pattern,
  $.or_pattern,
  $.and_pattern,
  $.null_pattern,
  $.type_test_pattern,
),

_simple_pattern: $ => choice(
  $.wildcard_pattern,
  $.literal_pattern,
  $.identifier_pattern,
  $.parenthesized_pattern,
),

wildcard_pattern: _ => '_',
literal_pattern: $ => choice($.int_literal, $.float_literal, $.char_literal, $.string_literal, $.bool_literal, $.unit, $.null_literal),
identifier_pattern: $ => choice($.identifier, seq($.identifier, $.pattern), seq($.long_identifier, optional($.pattern))),
parenthesized_pattern: $ => seq('(', $.pattern, ')'),
tuple_pattern: $ => seq($.pattern, ',', $.pattern, repeat(seq(',', $.pattern))),
list_pattern: $ => seq('[', optional(seq($.pattern, repeat(seq(';', $.pattern)))), ']'),
array_pattern: $ => seq('[|', optional(seq($.pattern, repeat(seq(';', $.pattern)))), '|]'),
record_pattern: $ => seq('{', $.field_pattern, repeat(seq(';', $.field_pattern)), '}'),
field_pattern: $ => choice($.identifier, seq($.identifier, '=', $.pattern)),
cons_pattern: $ => prec.right(seq($.pattern, '::', $.pattern)),
as_pattern: $ => prec.right(seq($.pattern, 'as', $.identifier)),
or_pattern: $ => prec.right(seq($.pattern, '|', $.pattern)),
and_pattern: $ => prec.right(seq($.pattern, '&', $.pattern)),
null_pattern: _ => 'null',
type_test_pattern: $ => seq(':?', $.type_expression, optional(seq('as', $.identifier))),
```

**Corpus tests:**
- `let _ = 1` (wildcard)
- `let (a, b) = pair` (tuple pattern)
- `let [x; y] = xs` (list pattern)
- `match x with | Some v -> v | None -> 0`

**Validation:** Pattern tests pass.

---

## Step 16 — Match arms

**What:** Add `match_arm` rule (already referenced in `match_expression` and `try_expression`).

**grammar.js:**
```js
match_arm: $ => seq('|', $.pattern, optional(seq('when', $.expression)), '->', $.expression),
```

**Corpus tests:**
- `| _ -> 0`
- `| x when x > 0 -> 1`
- `| Some v -> v`

**Validation:** Match arms parse with optional `when` guard.

---

## Step 17 — Type expressions (minimal)

**What:** Add minimal type expressions for annotations (`: int`, `: 'a -> 'b`, etc.).

**grammar.js additions:**
```js
type_expression: $ => choice($.function_type, $.tuple_type, $._primary_type),

function_type: $ => prec.right(seq($._primary_type, '->', $.type_expression)),

tuple_type: $ => prec.right(seq($._primary_type, '*', $._primary_type, repeat(seq('*', $._primary_type)))),

_primary_type: $ => choice(
  $.named_type,
  $.type_parameter,
  $.parenthesized_type,
  $.array_type,
  $.list_type,
  $.option_type,
  $.wildcard_type,
),

named_type: $ => seq(choice($.identifier, $.long_identifier), optional($.type_arguments)),
type_arguments: $ => seq('<', $.type_expression, repeat(seq(',', $.type_expression)), '>'),
type_parameter: _ => seq(choice("'", '^'), /[a-zA-Z_][a-zA-Z0-9_']*/),
parenthesized_type: $ => seq('(', $.type_expression, ')'),
array_type: $ => seq($._primary_type, '[]'),
list_type: $ => seq($._primary_type, 'list'),
option_type: $ => seq($._primary_type, 'option'),
wildcard_type: _ => '_',
```

**Corpus tests:**
- `let x : int = 1`
- `let f : 'a -> 'b = ...`
- `let t : int * string = ...`

**Validation:** Type annotations parse correctly.

---

## Step 18 — Open statements and hash directives

**What:** Add top-level open and hash directives.

**grammar.js additions:**
```js
open_statement: $ => seq('open', optional('type'), $._name),

hash_directive: _ => seq('#', /[^\r\n]+/),

_name: $ => choice($.identifier, $.long_identifier),
```

**Update `source_file`:**
```js
source_file: $ => seq(repeat($.hash_directive), repeat($._top_level_item)),

_top_level_item: $ => choice($.let_binding, $.do_expression, $.open_statement, $.expression),
```

**Corpus tests:**
- `open System`
- `open type System.Math`
- `#light` (hash directive)

**Validation:** Top-level items parse correctly.

---

## Step 19 — highlights.scm captures

**What:** Map every Phase 1 node type to a Helix highlight capture.

**queries/highlights.scm additions:**
```scheme
; Keywords
"let" @keyword
"rec" @keyword
"in" @keyword
"if" @keyword
"then" @keyword
"else" @keyword
"match" @keyword
"with" @keyword
"when" @keyword
"function" @keyword
"fun" @keyword
"try" @keyword
"finally" @keyword
"raise" @keyword
"reraise" @keyword
"do" @keyword
"for" @keyword
"while" @keyword
"to" @keyword
"downto" @keyword
"type" @keyword
"of" @keyword
"inherit" @keyword
"mutable" @keyword
"lazy" @keyword
"open" @keyword
"module" @keyword
"namespace" @keyword
"new" @keyword
"struct" @keyword
"class" @keyword
"interface" @keyword
"end" @keyword
"abstract" @keyword
"default" @keyword
"override" @keyword
"virtual" @keyword
"static" @keyword
"member" @keyword
"val" @keyword
"internal" @keyword
"private" @keyword
"public" @keyword
"extern" @keyword
"void" @keyword
"return" @keyword
"upcast" @keyword
"downcast" @keyword
"begin" @keyword
"sig" @keyword
"global" @keyword
"inline" @keyword
"assert" @keyword
"fixed" @keyword
"as" @keyword
"base" @keyword
"null" @constant.builtin
"true" @boolean
"false" @boolean

; Literals
(int_literal) @number
(float_literal) @number.float
(char_literal) @character
(string_literal) @string
(verbatim_string) @string
(triple_quoted_string) @string
(bool_literal) @boolean
(unit) @constant.builtin
(null_literal) @constant.builtin

; Comments
(line_comment) @comment
(block_comment) @comment

; Identifiers
(identifier) @variable
(long_identifier) @variable

; Operators
(infix_op) @operator
(custom_op) @operator

; Access modifiers
(access_modifier) @keyword
```

**Note:** The SPEC mentions many capture names that don't map directly to Helix's standard captures. We'll use Helix's standard names (`@keyword`, `@number`, `@string`, `@comment`, `@operator`, `@variable`, `@boolean`, `@constant.builtin`).

**Validation:** Run `helix --health fsharp` and verify `Highlight queries: ✓`.

---

## Step 20 — Corpus tests (30+ tests)

**What:** Write comprehensive corpus tests covering all Phase 1 features.

**File:** `test/corpus/phase1_lexer.txt`

**Test categories:**
1. Literals (10 tests): int, float, char, string, verbatim, triple-quoted, bool, unit, null, hex/oct/bin
2. Comments (3 tests): line, block, nested block
3. Identifiers (2 tests): simple, long, with apostrophe
4. Let bindings (5 tests): value, function, recursive, typed, with access modifier
5. Expressions (15+ tests): arithmetic, boolean, comparison, cons, append, pipe, compose, application, dot, index, unary, if, match, lambda, sequential, tuple
6. Collections (4 tests): list, array, record, parenthesized
7. Patterns (5 tests): wildcard, literal, tuple, list, cons
8. Match arms (3 tests): simple, with when, with union case
9. Open statements (2 tests): simple, with type

**Validation:** `tree-sitter test` passes all 30+ tests.

---

## Step 21 — Helix end-to-end validation

**What:** Copy grammar to Helix and verify highlighting works.

```bash
./dev.sh
helix --health fsharp
```

**Expected:**
- Tree-sitter parser: ✓
- Highlight queries: ✓
- No errors in `~/.cache/helix/helix.log`

**Visual check:** Open an F# file in Helix and confirm:
- `let`, `if`, `match`, etc. are colored as keywords
- String literals are colored as strings
- Numbers are colored as numbers
- Comments are colored as comments

---

## Decision log (append as we go)

| # | Decision | Rationale |
|---|----------|-----------|
| 1 | Keywords are inline strings, not a unified `keyword` rule | Avoids ambiguity; allows keywords to be distinguished by context |
| 2 | `word: $ => $.identifier` | Makes keywords non-overlapping with identifiers in most positions |
| 3 | Precedence levels numbered 1-18 (low→high) | Clear ordering; easy to insert new levels between existing ones |
| 4 | Block comments use recursive rule (not external scanner) | Tree-sitter supports recursive rules for nested structures |
| 5 | Patterns inline with expressions (Phase 1) | Needed for let bindings and match arms; full pattern disambiguation deferred |
| 6 | Type expressions minimal in Phase 1 | Only needed for `:` annotations in bindings; full type grammar deferred to Phase 3 |

---

## Next phases (after Phase 1 is complete)

- **Phase 2:** Full pattern grammar, active patterns, match expression refinements
- **Phase 3:** Type definitions (records, unions, aliases, enums, classes, structs, interfaces)
- **Phase 4:** Modules, namespaces, attributes, module abbreviations
- **Phase 5:** OOP (class members, constructors, interface implementations, object expressions)
- **Phase 6:** Computation expressions, sequence/list/array comprehensions
- **Phase 7:** String interpolation, units of measure, quotations, exception definitions
- **External scanner:** INDENT/DEDENT tokens for offside-rule constructs
