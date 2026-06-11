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

## Fix 3 — over-indented continuation arms + |-led custom-operator continuations

Two scanner changes, found via PostInferenceChecks.fs (260 nodes):
1. **Over-indented arm**: FSC permits a continuation arm MORE indented than its
   match (`| _ -> ()` at col 8 attaching to a match whose arms sit at col 4 —
   it closes the inner if-body/arm-body first). New close rule in the layoutish
   case: a leading `|` (bar_arm) at EXACTLY the body column emits LAYOUT_END,
   gated on (a) not S_TYPEBODY (DU cases lead with `|` at body col), and (b) an
   enclosing S_MATCH further left on the stack (walk stops at a non-match
   context left of the bar — the bar would belong to that body instead).
2. **`|?>`-style continuations**: the leading-`|` infix detection knew only
   `|>`/`||`; `|?>` (FCS NameResolution) classified as an arm marker, and rule
   1 then closed the body it continued (25→29 regression caught by the sweep
   A/B diff). Now `|` + ANY operator char = infix continuation.

Bench: 250→249 files, 2873→2537 nodes (−336), 0 regressions, 0 worsened.
PostInferenceChecks.fs 260→0; TypedTreeOps/IlxGen/CheckExpressions improved.
Corpus 433 (+2), layout.fsx L27_OverIndentedArm + |?> example.

GOTCHA (again, third time): a scanner compile ERROR leaves the stale cached
parser silently answering — `tree-sitter test` surfaces the compile error,
plain `parse` does not. Check `tree-sitter test` after every scanner edit.

## Fix 4 — dangling else: inline if/else inside an indented then-body

`if a then⏎    if p then x else y⏎else …` (TaggedCollections/FCS style): the
scanner's mid-line `else` close greedily popped the OUTER then-body, handing
the inline else to the outer if and stranding the real `else` line. Externals
preempt keyword lexing, so GLR never saw the inner attachment — the fix must
be scanner-side.

Mechanism: `Ctx` gains an `inl` flag (fits in struct padding — serialization
layout unchanged) set when EXPR_OPEN/ELSE_OPEN open a body INLINE (same line
as its opener). The mid-line `else`/`elif` close now fires only on inline
bodies; an indented then-body is never closed by a same-line else, which by
construction belongs to an inner if. `in`/`end` keep the unconditional close.

Bench: 249→238 files, 2537→2310 nodes, 0 regressions. TaggedCollections.fs
158→0, TypeRelations.fs 16→0, 8 Paket files cleared. Corpus 434, layout L28.

## Fix 5 — block comment leading an inline body (`| A -> (* tailcall *) f res`)

peek_body_col treated a leading `(*…*)` after an opener (`->`, `=`, `then`) as
"body starts on a later line" and returned the NEXT line's indent as the body
column — for `| A -> (* tailcall *) f res` that's the next ARM's column, so the
arm body opened at the arm column and everything after collapsed. Now the
comment is skipped (depth-aware) and same-line content keeps the comment's
start column as the inline body column (mirroring next_line_indent's
comment-led-element rule); only a true end-of-line defers to next_line_indent.

Bench: 238→235 files, 2310→1883 nodes (−427), 0 regressions. DiagnosticsLogger
174→0, TypedTreeOps 180→0, ilwrite 73→0 (one tiny shared idiom carried three of
the biggest FCS files). Corpus 435, layout example added to L28's module file.

## Fix 6 — bracket containing only a block comment (`[(* no attributes *)]`)

The BRACKET_OPEN probe saw `(` as inline content and opened a block context, so
the grammar's block form demanded an element and the `]` errored. Now a leading
`(*…*)` is skipped (depth-aware): a closer after it = no context (empty
list/array), a newline defers to the block form, real content anchors at the
comment's column.

Bench: 235→233 files, 1883→1760 nodes, 0 regressions. CheckIncrementalClasses
84→0, fantomas CheckDeclarations 19→0. Corpus 436.
