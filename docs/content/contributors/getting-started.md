---
title: Getting started
order: 1
---

## Requirements

- Node.js. `npm ci` installs the tree-sitter CLI pinned in `package-lock.json`.
- The .NET SDK version listed in `global.json`.
- A C compiler (gcc, clang or MSVC) for the tree-sitter CLI to compile the parser.

## The build script

Every command is a subcommand of the F# project in `build/`, run through `./build.sh` (`build.bat` on Windows). `./build.sh --help` lists them.

| Command | What it does |
|---|---|
| `generate [--force]` | Regenerate `src/` and `signature/src/` from the two `grammar.js` files. |
| `build [--force]` | Compile `parser.so` and `signature/parser.so`, regenerating first when needed. |
| `test [--signature] [-i NAME] [-u]` | Corpus and highlight tests of both grammars. |
| `test-all` | Every local gate. |
| `dev helix` | Build and copy the parsers and queries into the Helix runtime. |
| `dev zed` | Refresh the Zed dev extension in `zed/`. |
| `dev nvim [FILE]` | Build the parser and open a file in the repo-local Neovim config. |
| `bench [--signature] [--summary] [--update-baseline]` | Sweep the pinned real-world repositories and compare with the baseline. |
| `score [--compare] [--update-baseline]` | Score the grammar on four quality axes. |
| `expansion [-i NAME]` | Run the expand-selection fixtures. |
| `check-queries` | Compile every query file against the current grammar. |
| `derive-queries [--check]` | Regenerate the Neovim, Zed and signature queries from the Helix ones. |
| `highlight-snapshot [--check]` | Write the resolved capture of every token of `examples/references.fsx`. |
| `highlight-coverage` | List the tokens with no highlight capture over a sample of the bench. |
| `docs [--watch] [--check]` | Build the documentation site in `docs/`. |

`generate` and `build` skip their work when the inputs did not change. The content hashes live in `.build-cache/`. Every command that loads a parser runs both first, so `build` is rarely typed by hand.

## Development loop

1. Edit `grammar.js`, `src/scanner.c` or a query in `queries/`.
2. `./build.sh test-all`.
3. `./build.sh bench` for the real-world regression gate. `--signature` sweeps the `.fsi` files with the signature grammar.
4. `./build.sh dev helix` or `./build.sh dev zed`, then restart Helix or rebuild the Zed extension.

## Repository layout

| Path | Content |
|---|---|
| `grammar.js`, `src/scanner.c` | The F# grammar and its external scanner. |
| `signature/` | The `.fsi` grammar, derived from `grammar.js`. Its scanner includes `src/scanner.c`. |
| `queries/` | Helix queries. `queries/nvim/`, `queries/zed/` and `queries/signature/` are derived or editor-specific. |
| `test/` | Corpus tests, highlight tests, expansion fixtures, bench and score baselines, the highlight snapshot. |
| `examples/` | `references.fsx` is the gallery every token of which is in the snapshot. `locals.fsx` exercises `locals.scm` in Helix. |
| `build/` | The F# build project. |
| `scripts/` | The end-user install scripts, the bench manifest and the Neovim-only highlight rules. |
| `zed/`, `nvim/` | The Zed dev extension and the isolated Neovim config. |
| `docs/` | The documentation site, built with [Nacara](https://mangelmaxime.github.io/Nacara/). Pages are Markdown files under `docs/content/`. |
| `LIMITATIONS.md` | Accepted trade-offs and known gaps, with the evidence behind each. |
