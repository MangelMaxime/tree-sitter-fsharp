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

## Fix 7 — attribute between the `module` keyword and the name

`module [<AutoOpen>]SeqTOperations =` (FSharpPlus style): module_decl had no
attribute slot after the keyword, so the name went MISSING and the body
collapsed. Added `repeat($.attribute)` after "module". (`type [<AutoOpen>]X`
already worked via the type-decl decoration path.)

Bench: 233→230 files, 1760→1678 nodes, 0 regressions. FSharpPlus Seq.fs 78→0,
both Feliz DateParsing clones 2→0. Corpus 437, layout L29.

## Fix 8 — nested block comments (external token) + attributes after `and`

1. **Nested `(* a (* b *) c *)`**: the block_comment token regex stops at the
   first `*)` — F# comments nest. block_comment/block_doc_comment are now ALSO
   external tokens (appended at the END of externals — enum indexes preserved);
   the internal regexes remain as the FALLBACK when the scanner declines (this
   external-name-matches-internal-rule fallback is core tree-sitter behavior).
   The scanner owns: (a) line-start comment-ONLY lines — next_line_indent's
   stop-mode consumes the whole comment with advance(false) and the boundary
   path emits it as one token BEFORE any close (sentinel 2, same machinery as
   PREPROC_INACTIVE); (b) same-line comments after code — emitted at the
   midline tail. Comment-LED lines (`(* 4 *) 7`) keep the geometry-first
   convention and fall back to the internal regex.
   HARD-WON RULES: (i) tree-sitter calls externals only at the token start —
   whitespace is skipped INSIDE the internal lex, so a probe gated on
   lookahead=='(' never fires after a space; (ii) advance(false) before a
   ZERO-WIDTH emission is harmless (token start commits, but zero-width tokens
   never re-mark) — this is what lets next_line_indent pre-consume `(*` to
   classify; (iii) mark_end may move ONLY on the path that emits the comment —
   marking before the comment-led geometry return made the next BRACKET_SEMI
   swallow the comment text (PriorityQueue aligned-arrays corpus caught it).
   KNOWN GAP: a NESTED comment on a comment-LED line truncates in the fallback.
2. **`and [<return: Struct>] (|BoolExpr|_|) =`**: let_and_binding and
   _and_decl_indented gained repeat($.attribute) after `and` (FCS IlxGen).

Bench: 230→228 files, 1678→1641 nodes, 0 regressions. fable-library-rust
Set.fs 24→0 (nested comments), IlxGen 48→45 (remaining: multi-line tuple
pattern in an arm — separate issue). Corpus 439 (+2).

## Fix 9 — fsyacc/fslex line directives (`# 14 "pars.fs"`)

Generated parser/lexer files interleave `# <num> "<file>"` directives at col 0
throughout the code. New `line_directive` extra token (`#` + digits + optional
quoted file), and next_line_indent skips those lines (plus `#line`) like the
`#if` family so they never participate in offside geometry.

Bench: 228→227 files, 1641→1551 nodes, 0 regressions. LexYacc lex.fs 42→0,
pars.fs 49→1; the fcs codegen pars.fs (31) has separate issues. Corpus 440,
layout L30, highlight @keyword.directive.

## Fix 10 — access modifier between ctor attributes and the param list

`type T [<ParamObject; Emit("$0")>]⏎    private (…) =` (Fable interop tests):
primary_constructor's attr branch gained `optional($.access_modifier)`, and the
CTOR_ATTR scanner gate now accepts ws/newlines + private/internal/public
between the attrs and the `(`.

Bench: 227→226 files, 1551→1512 nodes, 0 regressions. ImportTests.fs 39→0.
Corpus 441, layout L31.

## Fix 11 — member access on an object expression

`{new Foo() with member _.Bar = 1}.Run()` (Hopac continuation style):
object_expression added to _dot_object. Hopac.fs 43→5 (rest is separate).
Bench: 226 files, 1512→1474 nodes, 0 regressions. Corpus 442.

## Fix 12 — trailing ascription on an inline else body

`… then Some x.[n] else None : 'a option` (FSharpPlus Indexable): the final-else
clause took $._expression only; it now also accepts $.type_ascription_expression
(like _indented_or_inline_body). Indexable.fs 25→0. Bench: 226→225 files,
1474→1437 nodes, 0 regressions. Corpus 443.

## Fix 13 — empty anonymous record `{| |}`

anonymous_record_expression required at least one field; `blank()` added to the
choice. Cleared 5 farmer files (Cdn 20→0, Web 9→0, Dashboard, ResourceGroup,
CognitiveServices). Bench: 225→220 files, 1437→1398 nodes, 0 regressions.
Corpus 444.

Also PARKED this stretch (in memory project_fix_backlog): `let Ctor(a, b), rest
= …` destructuring (two attempts silently broke `let f(a, b) =` fn defs);
ReflectionTests.fs / General.fs / MSBuildHelper.fs / Chocolatey.fs are
compound (pieces pass in isolation, full files fail — context-dependent).
