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
