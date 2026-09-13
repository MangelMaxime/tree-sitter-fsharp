---
title: Queries
order: 4
---

`queries/` holds the Helix queries. They are the source of truth for the other editors.

| Directory | Content | Maintained |
|---|---|---|
| `queries/` | Helix: highlights, injections, locals, textobjects, indents, tags, rainbows. | By hand. |
| `queries/nvim/` | Neovim. `highlights.scm` is generated; `indents.scm`, `folds.scm`, `textobjects.scm` are written for Neovim. | Generated and by hand. |
| `queries/zed/` | Zed. `highlights.scm` is generated; `brackets`, `outline`, `overrides`, `indents`, `injections`, `textobjects` are written for Zed. | Generated and by hand. |
| `queries/signature/`, `queries/zed/signature/` | The patterns of the F# queries that compile against the signature grammar. | Generated. |

`./build.sh derive-queries` regenerates every generated file. `--check` fails when one is stale. Generated files start with a `GENERATED` header; edit the Helix file instead.

## Neovim highlights

The Neovim file is the Helix file with renamed captures, `#match?` turned into `#lua-match?` or `#any-of?`, and the rules of `scripts/nvim-highlights-extra.scm` appended. That file holds the Neovim-only groups, for example `@keyword.conditional` and `@keyword.repeat`.

## Zed highlights

The Zed file is the Helix file with renamed captures. Zed loads `queries/zed/<name>.scm` when it exists and the Helix file otherwise, except `indents.scm`, which never falls back because `@indent`, `@outdent` and `@extend` mean different things in Zed. Zed does not load `locals.scm`.

## Helix stubs

`./build.sh dev helix` writes an empty file for every query kind the repository does not define. Helix falls back to its built-in F# queries for a missing kind, and those do not match this grammar.
