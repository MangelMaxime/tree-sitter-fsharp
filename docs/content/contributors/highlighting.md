---
title: Highlighting
order: 3
---

`queries/highlights.scm` is the Helix query. The Neovim, Zed and signature queries are generated from it. The highlight tests in `test/highlight/` are the specification: every colour decision is pinned by an assertion, and the query is adjusted until the tests pass.

## Decisions

Syntax alone cannot tell a union case from a type, or a module from a class. `test/HIGHLIGHTING.md` holds the conventions the tests pin, one row per situation, with a status:

- `confirmed`: pinned by a reviewed test file.
- `trial`: agreed, not yet pinned by one.

To change a decision, edit the assertion in the test file and adjust the query. The tests lead.

## Capture names

The Helix names are the source. `build/Commands/DeriveQueries.fs` holds the two rename tables:

| Helix | Neovim | Zed |
|---|---|---|
| `keyword.control.import` | `keyword.import` | `keyword.control.import` |
| `keyword.storage.type` | `keyword.type` | `keyword.storage.type` |
| `keyword.storage.modifier` | `keyword.modifier` | `keyword.storage.modifier` |
| `namespace` | `module` | `type` |
| `variable.other.member` | `variable.member` | `property` |
| `function.method` | `function.method.call` | `function.method` |
| `constant.numeric.integer` | `number` | `number` |

Zed maps `namespace` to `type` because its bundled themes leave `namespace` at the text colour.

## Reviewing a colour

`./build.sh dev helix` deploys the current queries. Open the test file, or a throwaway `examples/scratch.fsx`, in Helix to see the colours in a theme. `./build.sh highlight-coverage` lists the tokens that get no capture at all over a sample of the bench, grouped by their syntactic context.

## Query rules that matter

- Editors resolve a token to the innermost capture that contains it; on equal ranges, the last pattern in the file wins. Order the file from general to specific.
- A pattern with an unanchored middle (`. (a) (b) (c) .`) produces two matches on a four-segment path and the highlighter keeps only one, so the middle node loses its capture. Write one fully anchored pattern per length.
- `([(a) (b)] (child))` is a sibling sequence, not "a or b with a child". Write `[(a (child)) (b (child))]`.
- `tree-sitter test` treats a `_`-prefixed capture as a highlight. Reuse the visible capture name in predicates instead.
