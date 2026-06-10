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
