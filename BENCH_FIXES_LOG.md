# Bench-fix session log — 2026-06-11 (autonomous, for review)

Branch: `wip/bench-fixes-2026-06-11` — ONE COMMIT PER FIX. Review with
`git log -p main..wip/bench-fixes-2026-06-11`; cherry-pick / drop per commit.
NOTE: never resolve conflicts in src/parser.c by hand — regenerate with
`tree-sitter generate grammar.js`.

Separately: `git stash list` holds the doc-comment attachment feature
("attach /// doc comments…") — its full design notes are in memory
(`project_bench2_sweep.md`, SHIPPED 2026-06-11 section).

Baseline at branch point (574b191+12 commits, main): /tmp/bench suite
(23 repos, 3654 files): 275 failing files / 3695 error nodes
(`/tmp/bench/sweep_b2e.txt`).

Gates per fix: corpus 428/428 green, examples/layout.fsx 0 errors,
full-bench sweep ZERO regressions (single-parse per file, repo cwd).

## Fixes

(appended below as they land)

## Fix 1 — `#if/#else` inactive-branch tokenization (PREPROC_INACTIVE)

The active `#if` branch parses as real code; the `#else`/`#elif` inactive region
becomes ONE trivia token (`preproc_inactive`, highlighted `@comment`), mirroring
how the F# compiler lexes the untaken branch. Grammar: externals + extras gained
`$.preproc_inactive`; `preproc_elif`/`preproc_else_kw` rules deleted. Scanner:
`next_line_indent` gets a `g_region_stop` mode — the MAIN line-boundary call
stops at an else/elif region (sentinel `first == 1`) and the boundary path
tokenizes the region up to (not including) the matching `#endif` line, BEFORE
any close/semi (extras are transparent; closes fire equally after). Peek callers
keep the region-skipping geometry.

Two failed designs documented for the record: (a) top-of-scan probe — consumed
whitespace unconditionally, broke consecutive-function files; (b) probes at the
switch's return-false tails — too late, next_line_indent has already advanced
past the region.

Bench: 275→258 files, 3695→3603 nodes, 0 regressions. Validation.fs 87→0,
NameResolution.fs 67(post-broken-probe)/25(baseline)→0, Logging.fs 20→0.
TaggedCollections +13 / DiagnosticsLogger +5 = recovery reshuffle in deeply
failing files (both shapes pass in isolation; first errors predate directives).
Corpus 429 (3 expected-tree updates + new consecutive-fns-with-#if-arms test),
layout.fsx L25_PreprocBranches.

MEASUREMENT WARNING (re-learned the hard way): `tree-sitter parse` MUST run with
cwd = THIS repo. A sweep that `cd`s into the bench repos reports 0 errors for
EVERYTHING (no grammar resolves — false pass). A "275→0 all clean" result is
the bug, not a miracle.

## Fix 2 — operator-as-value forms + as-pattern in let tuples (Optimizer.fs 630→0)

Three related gaps, found by peeling Optimizer.fs:
1. **Qualified operator value** `Unchecked.(+)` / `A.B.C.(>>=)`: new
   `qualified_operator_expression` = `long_identifier` + `operator_member`,
   where the tail `.(+)` is ONE token (same trick as `active_pattern_member`,
   so it never competes with a long_identifier's own `.`). Wired into both
   `_simple_expression` (arg slots) and `_expression` (standalone/head).
   Spaces allowed: `Unchecked.( * )` (spaced to dodge the `(*` comment opener).
2. **`(~~~)` as a value**: `unary_expression`'s STRING token "~~~" beats
   symbolic_op's regex at the lexer, so `operator_name`/`_value_operator_name`
   list "~~~" explicitly — identical string tokens unify, and GLR picks by
   what follows (`)` = operator name, expr = unary). `( * )` added likewise.
3. **as-pattern as a tuple ELEMENT** in let bindings (`let bindR, binfo as
   bindInfo, env = …`): F# binds `as` tighter than `,` here
   (headBindingPattern). New `as_tuple_elem_pattern` (prec.right 1, aliased to
   `as_pattern`) in `_tuple_elem_pattern` + standalone in `_let_name_pattern`
   (the prec bump diverts `let a as b` from its old route).

Bench: 258→250 files, 3603→2873 nodes (−730), 0 regressions, 0 worsened.
Optimizer.fs 628→0, PrimitivesTests 17→0, BigInt.fs 13→0. Corpus 431
(+2 tests), layout.fsx L26_OperatorValues, highlights: (operator_member)
@operator + "~~~"/"*" in operator_name.
