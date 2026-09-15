---
title: Getting started
order: 1
---

Everything runs through one script. This page takes you from a clone to a passing test run.

## Requirements

- Node.js. `npm ci` installs the tree-sitter CLI pinned in `package-lock.json`.
- The .NET SDK version listed in `global.json`.
- A C compiler (gcc, clang or MSVC). The tree-sitter CLI uses it to compile the parser.

## First run

```bash
git clone https://github.com/MangelMaxime/tree-sitter-fsharp
cd tree-sitter-fsharp
npm ci
./build.sh test-all
```

On Windows, use `build.bat` instead of `./build.sh`.

The first run generates and compiles both parsers, then runs every gate. Later runs skip generating and compiling when `grammar.js` and `src/scanner.c` did not change.

## A change, start to finish

1. Edit `grammar.js`, `src/scanner.c` or a query in `queries/`.
2. Add a corpus test or a highlight assertion that shows the change, see [Testing](testing.md).
3. `./build.sh test-all`.
4. `./build.sh bench`. A real-world file that parses worse than before fails it.
5. `./build.sh dev <editor>`, with `helix`, `zed` or `nvim`, and look at the result in that editor.
6. Commit with a Conventional Commit message, see [Releasing](releasing.md).

## The commands

`./build.sh --help` lists them, and `./build.sh <command> --help` shows the options of one.

**Every day**

| Command | What it does |
|---|---|
| `test-all` | Every local gate: corpus, highlight assertions, examples, expansion fixtures, query checks, derived queries, snapshot. |
| `test` | Corpus and highlight tests only. `-i NAME` runs the tests whose name matches, `-u` rewrites their expected trees, `--signature` runs the signature grammar alone. |
| `bench` | Parse the 24 pinned repositories and compare with the baseline. `--summary` adds a per-project table, `--update-baseline` accepts improvements, `--signature` sweeps the `.fsi` files. |
| `dev helix`, `dev zed`, `dev nvim [FILE]` | Put the current grammar and queries in an editor. |

**When needed**

| Command | What it does |
|---|---|
| `expansion` | The expand-selection fixtures alone, when a change moves where a node starts or ends (doc comments, attributes, declarations). `-i TEXT` runs the fixtures whose name contains it. |
| `check-queries` | Compile every query file against the grammar. |
| `derive-queries` | Regenerate the Neovim, Zed and signature queries from the Helix ones. `--check` only reports stale files. |
| `highlight-snapshot` | Rewrite `test/highlight-snapshot.txt`. `--check` only reports a difference. |
| `highlight-coverage` | List the tokens that get no colour over a sample of the bench. `--files N` and `--top N` size it. |
| `score` | The four quality axes. `--compare` fails on a regression, `--update-baseline` accepts the current scores. |
| `generate`, `build` | Regenerate `src/` and compile the parsers. `--force` ignores the cache. Every command that loads a parser runs them first, so they are rarely typed. |
| `docs` | Build this site. `--watch` serves it on `http://localhost:8080`, `--check` builds without writing, `--deploy` publishes it to the `gh-pages` branch (CI does this on every push to `main`). |
| `zed-extension` | Open or update the pull request that pins a release in the Zed extension, see [Releasing](releasing.md). |

## Where things are

| Path | Content |
|---|---|
| `grammar.js`, `src/scanner.c` | The F# grammar and its external scanner. |
| `signature/` | The `.fsi` grammar, derived from `grammar.js`. |
| `queries/` | The Helix queries, the source for every other editor. |
| `test/` | Corpus tests, highlight tests, expansion fixtures, bench and score baselines, the highlight snapshot, `HIGHLIGHTING.md`. |
| `examples/` | `references.fsx`, a gallery of F# constructs, and `locals.fsx`, for checking `locals.scm` in Helix. |
| `build/` | The F# project behind `./build.sh`. |
| `scripts/` | The install scripts for users, the bench manifest, the Neovim-only highlight rules. |
| `zed/`, `nvim/` | The Zed development extension and the isolated Neovim config. |
| `docs/` | This site, built with [Nacara](https://mangelmaxime.github.io/Nacara/). Pages are Markdown files under `docs/content/`. |
| `LIMITATIONS.md` | Accepted trade-offs and known gaps, with the evidence behind each. |
