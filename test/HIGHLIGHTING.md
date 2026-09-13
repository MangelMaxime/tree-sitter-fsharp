# Highlight conventions

The files in `test/highlight/` are the specification of the highlighting. Every capture
decision is pinned by an assertion; `queries/highlights.scm` is adjusted until they pass.
Neovim and Zed queries derive from the Helix one (`task queries:derive`). Zed maps
`namespace` to `type` because its bundled themes leave `namespace` at the text colour.

## How to review

1. Read a test file top to bottom. Each line of code is followed by `// ^ capture`
   assertions for the tokens that carry a decision.
2. To overrule a decision, edit the assertion. The queries follow the tests, not the
   other way round.
3. Plain tokens (no capture) are listed in the file header, since the assertion format
   cannot express "no capture".

An assertion passes when the expected name is one of the captures at that position.
Helix uses the last capture in source order, so `test/highlight-snapshot.txt` records the
resolved capture of every token in `examples/references.fsx`; `task highlight:snapshot`
regenerates it and the diff is reviewed like any other change.

## Decisions

Syntax alone cannot tell a union case from a type or a module from a class. The
convention below is what the tests pin. `trial` means agreed to try and review in the tests; `confirmed` is settled.

| # | Situation | Capture | Status |
|---|---|---|---|
| D1 | `let f x = ...` (has parameters) | name `function`, parameters `variable.parameter` | confirmed |
| D2 | `let x = ...` (no parameters) | name `function`: a value is often a partial application or a lambda, so one colour for every `let` name; a `use` name is `variable` (always a disposable value) | confirmed |
| D3 | `List.map xs`, `System.IO.Path.Combine(a, b)`, `Inner.v` | every capitalised qualifier `namespace`; the called segment `function`; a lowercase last segment that is not called (`Q.value`, `List.map` as an argument) `function` too, see D2 | confirmed |
| D4 | `x.Length`, `x.ToString()`: member access on a value | member `variable.other.member`, called member `function.method` | trial |
| D5 | Bare capitalised name in an expression (`None`, `Aligned`) | `constructor` | trial |
| D6 | `Result.Error x`, `File.OpenRead p`: qualified name applied | qualifier `namespace`, last segment `function` whatever its case: static method calls are far more common than applied qualified constructors | trial |
| D7 | Capitalised single segment in a type position | `type` | confirmed |
| D8 | `int`, `string`, `unit`, `bool`, `float`, `obj`, `exn`... | `type.builtin` | trial |
| D9 | CE builder (`async { }`, `task { }`) | `keyword` (current) | trial |
| D10 | `if`/`then`/`elif`/`else`/`match`/`with`/`when` | `keyword.control.conditional` | trial |
| D11 | `for`/`while`/`to`/`downto`/`do` in loops | `keyword.control.repeat` | trial |
| D12 | `return`/`yield` and the `!` forms | `keyword.control.return` | trial |
| D13 | `open` | `keyword.control.import`; `#r`, `#load`, `#nowarn` stay `keyword.directive` | trial |
| D14 | `try`/`with`/`finally`/`raise`/`failwith`/`reraise` | `keyword.control.exception` | trial |
| D15 | `fun`, `function` | `keyword.function` | trial |
| D16 | `type`, `module`, `namespace`, `exception` | `keyword.storage.type` | trial |
| D17 | `mutable`, `inline`, `static`, `abstract`, `override`, `rec`, access modifiers | `keyword.storage.modifier` | trial |
| D18 | `_` in patterns and parameters | `wildcard` (current, theme-neutral) | trial |
| D19 | `[<Literal>] let X = 1` | declaration name `constant`; uses cannot be told from other values | trial |
| D20 | `this`/`self` identifier in members, `base` | `variable.builtin` | confirmed |
| D21 | Doc comments `///`, `(** *)` | `comment.line.documentation` / `comment.block.documentation` | confirmed |
| D22 | Format specifiers `%d`, interpolation holes | `string.special`, braces `punctuation.special` | confirmed |
| D23 | Operators: custom symbolic, pipes, `<-`, `:=`, and the name in `let (+++) a b` | `operator` | confirmed |
| D24 | `[<Attr>]` names | `attribute` | confirmed |
| D25 | `int * string`, `int -> int`, `#A & #B`: type-level operators | `keyword`; the lambda and match arrows stay `keyword.control` | confirmed |
| D26 | Inline IL `(# "cgt" x y : int #)` | `(#` and `#)` `punctuation.special` like quotation brackets, the rest as usual | confirmed |

## Open questions

Each question names the file and line to look at, in Helix after `task dev:helix` or in Zed
after a rebuild of the dev extension. A throwaway `examples/scratch.fsx` can hold the code to look at.

| # | Question | Where to look |
|---|---|---|

## Files

One file per family. `done` means the file is written, reviewed and passing.

| File | Pins | Status |
|---|---|---|
| bindings.fsx | D1, D2, D17, D19, D23, D25, `use`, `and`, `rec` | review |
| patterns.fsx | D5, D6, D18, records, lists, type tests, active patterns | planned |
| modules.fsx | D3, D13, D16, namespaces, opens, qualified access | review |
| members.fsx | D4, D20, properties, methods, abstract, interfaces, constructors | planned |
| types.fsx | D7, D8, generics, constraints, measures, records, unions, enums | planned |
| control_flow.fsx | D10, D11, D12, D14, D15 | planned |
| computation_expressions.fsx | D9, `let!`, `do!`, query operators, custom operations | planned |
| literals_strings.fsx | D22, numbers, chars, verbatim, triple, interpolated | planned |
| comments_attributes.fsx | D21, D24, preprocessor, `#nowarn` | planned |
| operators.fsx | D23, prefix, quotations, splices, indexers | planned |
| signature/test/highlight/signatures.fsi | member signatures, `val`, `new:` | planned |

The five existing files (`basics`, `literals_comments`, `misc_captures`, `structure`,
`values`) are folded into these and then deleted.

## Tooling

- `task test` runs the assertions with the corpus.
- `task highlight:snapshot` writes `test/highlight-snapshot.txt` (resolved capture per
  token of `examples/references.fsx`); `-- --check` fails CI when it drifts.
- `task highlight:coverage` lists tokens with no capture over the bench sample, grouped
  by context, so a family with a hole is visible before it is reviewed.
- `task dev:helix` deploys to Helix to see a file in a real theme.
