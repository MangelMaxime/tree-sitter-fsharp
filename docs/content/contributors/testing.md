---
title: Testing
order: 2
---

`./build.sh test-all` runs every check on this page except the bench and the score. On each push and pull request, CI runs `test-all` on Linux, macOS and Windows, and the bench and the score on Linux.

## Corpus tests

`test/corpus/*.txt`, one file per topic. A test is a snippet and the tree it must produce:

```
================================================================================
Simple let binding
================================================================================

let answer = 42

--------------------------------------------------------------------------------

(source_file
  (let_binding
    name: (identifier)
    body: (int_literal)))
```

To add one, write the snippet with an empty tree, let the parser fill it in, then read what it wrote:

```bash
./build.sh test -u -i "Simple let binding"
./build.sh test -i "Simple let binding"
```

:::warning
`-u` overwrites the expected tree of every test it runs. Always pair it with `-i`, and read the diff before committing.
:::

The signature grammar has its own corpus in `signature/test/corpus/`. `./build.sh test --signature` runs it alone.

## Highlight tests

`test/highlight/*.fsx` define the colours. Each line of code is followed by comments that assert the colour of its tokens:

```fsharp
let add a b = a + b
// <- keyword
//  ^ function
//      ^ variable.parameter
```

`^` checks the token above the caret. `<-` checks the token above the first `/` of the comment. The name is the full capture name, for example `comment.line.documentation`.

`test/HIGHLIGHTING.md` lists every colour decision and the file that pins it. [Highlighting](highlighting.md) explains how to change one.

A `.fsi` file in `test/highlight/` is checked against the signature grammar and `queries/signature/highlights.scm`: `tree-sitter.json` maps each file extension to its grammar.

## Examples

Every file in `examples/` must parse without an error.

## Expansion tests

Helix's expand-selection grows the selection one tree node at a time, and the corpus tests cannot check where a node starts or ends. `test/expansion/*.txt` place a `‸` cursor in a snippet and list the text selected at each step, for example that a binding is selected before its `///` doc comment.

Run them on their own while changing how doc comments, attributes or declarations attach:

```bash
./build.sh expansion
./build.sh expansion -i "union case"
```

## Query checks

Three checks catch what `tree-sitter test` does not:

- **`check-queries`** compiles every `.scm` file against the grammar. `tree-sitter test` only loads `highlights.scm` and `locals.scm`, so the other files could name a node that no longer exists and nobody would notice until an editor loads them.
- **`derive-queries --check`** fails when a generated query file was edited by hand or is out of date.
- **`highlight-snapshot --check`** fails when `test/highlight-snapshot.txt` no longer matches the queries. The snapshot records the colour of every token of `examples/references.fsx`, so a query change that moves a colour anywhere shows up as a diff. Regenerate it with `./build.sh highlight-snapshot` and review that diff.

## Bench

`./build.sh bench` parses every `.fs` and `.fsx` file of the 24 repositories listed in `scripts/bench-manifest.txt`, about 3 900 files including the F# compiler's `src/`. It compares each file's error count with `test/bench/baseline.txt`, and a file that parses worse fails the run.

```bash
./build.sh bench                    # sweep and compare
./build.sh bench --summary          # add the per-project table
./build.sh bench --update-baseline  # accept improvements into the baseline
./build.sh bench --signature        # the .fsi files, with the signature grammar
```

The first run clones the repositories, about 1.5 GB, into `~/.cache/fsharp-grammar-bench`. Set `FSHARP_BENCH_DIR` to use another directory.

:::note
Error counts come from an in-process parser and can differ from `tree-sitter parse` on files that already have errors. Compare a baseline only with `./build.sh bench`.
:::

## Score

`./build.sh score` measures the grammar on four axes over the bench corpus. `--compare` fails on a regression against `test/score/baseline.json`.

| Axis | Question it answers |
|---|---|
| coverage | Does valid F# parse without errors? |
| rejection | Is invalid F# flagged? Measured as recall on syntax errors, and false positives on code that is only a type error. |
| degeneracy | How often is a keyword read as a plain identifier in a file that otherwise parses cleanly? |
| highlight | What share of the captures in `queries/highlights.scm` does a highlight test check? |
