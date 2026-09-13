---
title: Testing
order: 2
---

`./build.sh test-all` runs every gate below except the bench and the score. CI runs it on Linux, macOS and Windows, then the bench and the score on Linux.

## Corpus tests

`test/corpus/*.txt`, one file per topic. Each test is a snippet and its expected tree:

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

To add one, write the section with an empty tree, generate it from the parser and review it:

```bash
./build.sh test -u -i "Simple let binding"
./build.sh test -i "Simple let binding"
```

`-u` overwrites the expected trees of every test it runs. Always filter it with `-i` and read the diff before committing.

The signature grammar has its own corpus in `signature/test/corpus/`. `./build.sh test --signature` runs it alone.

## Highlight tests

`test/highlight/*.fsx` are the specification of the highlighting. They use tree-sitter's assertion comments:

```fsharp
let add a b = a + b
// <- keyword
//  ^ function
//      ^ variable.parameter
```

`<-` asserts the capture of the token at the start of the line above. `^` asserts the token at its own column. The expected name is the full capture name, for example `comment.line.documentation`.

`test/HIGHLIGHTING.md` lists the decisions the tests pin and the files that pin them. See [Highlighting](highlighting.md).

## Examples

Every file in `examples/` must parse without an error node.

## Expansion tests

Helix's expand-selection walks node extents, which the corpus tests cannot assert. `test/expansion/*.txt` pin the selection text of each expansion step from a `‸` cursor marker.

```bash
./build.sh expansion
./build.sh expansion -i multiDoc
```

## Query checks

`check-queries` compiles every `.scm` file against the current grammar. `tree-sitter test` only loads `highlights.scm` and `locals.scm`; the other files can name a node that no longer exists and stay silent until an editor loads them.

`derive-queries --check` fails when a derived query file was edited by hand or is stale.

`highlight-snapshot --check` fails when `test/highlight-snapshot.txt` differs from the current queries. The snapshot holds the resolved capture of every token of `examples/references.fsx`; a query change that moves a colour shows up as a diff there. Regenerate it with `./build.sh highlight-snapshot` and review the diff.

## Bench

`./build.sh bench` parses every `.fs` and `.fsx` file of the 24 repositories pinned in `scripts/bench-manifest.txt` (about 3 900 files, including the F# compiler's `src/`) and compares the per-file error counts with `test/bench/baseline.txt`. Any file that parses worse than the baseline fails the run.

```bash
./build.sh bench
./build.sh bench --summary
./build.sh bench --update-baseline
./build.sh bench --signature
```

The first run clones the repositories (about 1.5 GB) into `~/.cache/fsharp-grammar-bench`, or `$FSHARP_BENCH_DIR`.

The sweep loads `parser.so` in-process and parses as UTF-16. Error counts on a file that already has errors can differ from the CLI's UTF-8 parse, so compare baselines only against `./build.sh bench`.

## Score

`./build.sh score` reports four axes over the bench corpus. `--compare` fails on a regression against `test/score/baseline.json`.

| Axis | Meaning |
|---|---|
| coverage | Valid F# parses without an error site. |
| rejection | Invalid F# is flagged: recall on syntax diagnostics, false positives on type-only ones. |
| degeneracy | Reserved keywords lexed as `identifier` in files with no error site. |
| highlight | Fraction of the captures in `queries/highlights.scm` that a highlight test pins. |
