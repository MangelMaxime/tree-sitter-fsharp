---
title: Highlighting
order: 3
---

The highlight tests decide the colours, and the queries follow them. To change a colour, change a test first.

## Where a colour is decided

`queries/highlights.scm` is the Helix query. The Neovim, Zed and signature queries are generated from it, so it is the only highlight query to edit.

`test/HIGHLIGHTING.md` is the decision table. It has one row per situation that syntax alone cannot settle, such as whether `Error` is a union case or a type, with the colour chosen and a status:

- `confirmed`: checked by a reviewed test file in `test/highlight/`.
- `trial`: agreed, not yet checked by one.

## Changing a colour

1. Edit or add the assertion in `test/highlight/`.
2. Adjust `queries/highlights.scm` until `./build.sh test` passes.
3. Run `./build.sh derive-queries` and `./build.sh highlight-snapshot`, then read the snapshot diff: it lists every token whose colour moved.
4. Update the row in `test/HIGHLIGHTING.md`.
5. Run `./build.sh dev helix` and look at the result in a theme.

To find tokens nobody coloured yet, `./build.sh highlight-coverage` lists the ones with no capture over a sample of the bench, grouped by where they appear.

## Capture names per editor

Captures are written with Helix names and renamed for the other editors by `build/Commands/DeriveQueries.fs`. The names that differ:

| Helix | Neovim | Zed |
|---|---|---|
| `keyword.control.import` | `keyword.import` | unchanged |
| `keyword.storage.type` | `keyword.type` | unchanged |
| `keyword.storage.modifier` | `keyword.modifier` | unchanged |
| `namespace` | `module` | `type` |
| `variable.other.member` | `variable.member` | `property` |
| `function.method` | `function.method.call` | unchanged |
| `constant.numeric.integer` | `number` | `number` |

Zed uses `type` for `namespace` because its bundled themes give `namespace` the plain text colour.

## Query pitfalls

- **Which capture wins.** A token takes the innermost capture that contains it. When two captures cover the same range, the pattern later in the file wins, so write general rules first and exceptions after.
- **Unanchored middle segments.** A pattern such as `(long_identifier . (identifier) (identifier) (identifier) .)` can match a four-segment path in two ways, and the highlighter keeps only one match, so a middle segment loses its colour. Write one fully anchored pattern per length instead.
- **Alternatives with a child.** `([(a) (b)] (child))` means "a or b, followed by a sibling child", not "a or b containing child". Write `[(a (child)) (b (child))]`.
- **Private captures.** `tree-sitter test` reports a `_`-prefixed capture like any other. In a predicate, reuse the visible capture name instead of introducing `@_name`.
