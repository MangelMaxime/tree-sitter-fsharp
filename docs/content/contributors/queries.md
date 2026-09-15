---
title: Queries
order: 4
---

Every query starts as a Helix query in `queries/`. Each other editor gets a generated copy, the Helix file itself, or a file written for it.

| Directory | Content | Edit by hand? |
|---|---|---|
| `queries/` | Helix: highlights, injections, locals, textobjects, indents, tags, rainbows. | Yes. |
| `queries/nvim/` | `highlights.scm` is generated. `indents.scm`, `folds.scm` and `textobjects.scm` are written for Neovim. | Only those three. |
| `queries/zed/` | `highlights.scm` is generated. `brackets`, `indents`, `injections`, `outline`, `overrides` and `textobjects` are written for Zed. | Only those six. |
| `queries/signature/`, `queries/zed/signature/` | The F# patterns that also apply to the signature grammar. | No. |

`./build.sh derive-queries` regenerates every generated file. A generated file starts with a `GENERATED` header, which is the quickest way to tell.

## Neovim

The Neovim highlights are the Helix highlights with three changes: captures are renamed, `#match?` becomes `#lua-match?` or `#any-of?`, and the rules in `scripts/nvim-highlights-extra.scm` are appended. That file holds the groups Neovim themes style and Helix has no name for, such as `@keyword.conditional` and `@keyword.repeat`.

For the other kinds, Neovim uses the file in `queries/nvim/` when there is one, and the Helix file otherwise.

## Zed

The Zed highlights are the Helix highlights with renamed captures. For the other kinds, Zed uses the file in `queries/zed/` when there is one, and the Helix file otherwise, with two exceptions:

- `indents.scm` never falls back to Helix: `@indent`, `@outdent` and `@extend` mean different things in Zed.
- `locals.scm` is never copied: Zed does not load it.

## Helix

Helix falls back to its built-in F# queries for any query kind it cannot find, and those do not match this grammar. `./build.sh dev helix` and the user install script therefore write an empty file for every kind the repository does not ship.
