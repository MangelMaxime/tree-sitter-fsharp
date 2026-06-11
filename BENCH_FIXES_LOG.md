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

## Fix 14 — empty block bracket + ascribed paren-tuple param element

1. `Html.div [`⏎ blank ⏎`]` (Feliz tests): the BRACKET_OPEN block path now
   declines when the next real char closes the bracket — no element, no context.
2. `(a, (g2, s2): Lens<'a,'b>)` (Aether lens compose, 4 vendored copies):
   tuple_typed_pattern's pattern slot accepts $.tuple_pattern.

Bench: 220→209 files, 1398→1268 nodes, 0 regressions. 11 files cleared
(4× Aether, 6× Feliz ReactBindings tests, 1 more). Corpus 446.

## Fix 15 — multi-line quotations with the closer at the body column

`<@`⏎`    body`⏎`@>` (FSharp.Data type providers): the leading `@>`/`@@>` line
got a LAYOUT_SEMI at the body column, orphaning the closer. The leading-`@`
continuation check now returns false (no token) for `@>`/`@@>` at exactly the
body column — the closer belongs to the still-open quotation; a DEDENTED closer
still lets the layout close fire first. (First attempt patched semi_blocked —
wrong: the infix peek has already consumed the `@`, so position assumptions
there are invalid.)

Bench: 209→207 files, 1268→1225 nodes, 0 regressions. WorldBankProvider 13→0,
HtmlGenerator 4→0, Helpers.fs 23→2. Corpus 447.

## Fix 16 — inline single-member type body

`type Lift = static member inline Invoke (x) = …` / `type A() = member _.Value
= 5`: _type_decl_body_or_class gained a bare $._class_body_member alternative
(no layout open fires for a same-line body). Two shift/reduce conflicts with
the augmentation `with` resolved via prec.right on abstract_member_defn and on
member_defn's auto-property branch (accessors win the inline `with`).

This was the REAL issue behind the "compound" ReflectionTests files — the
lesson: a whole-file failure anchored far from its cause can be a tiny inline
form; test the SMALL declarations near the first error too, not just the big
constructs.

Bench: 207→197 files, 1225→1130 nodes, 0 regressions. 10 files cleared
(3× ReflectionTests, MonadTrans 17→0, Tuple, Functor, Enumerator…). Corpus 448.

## Fix 17 — P/Invoke `extern` declarations

`[<DllImport("Kernel32")>]`⏎`extern bool private GetConsoleMode(void* _h, int*
_mode)` (expecto Logging, FAKE Process/integration tests): new extern_decl in
the module-level decl list — C-style return/param types as long_identifier +
`*`/`&`/`[]` suffixes, optional access modifier, `[<Out>]`-style param attrs.
Leading attributes parse as standalone items (a repeat($.attribute) inside the
rule conflicted with module_decl's). "extern" added to the keyword highlights.

Bench: 197→195 files, 1130→1127 nodes, 0 regressions; ProvidedTypes +14 /
IlxGen +7 are recovery reshuffle (both contain `extern` ONLY in comments; both
already fail massively — their states shifted, not their real parses).
Logging.fs 18→0. Corpus 449.

## Fix 18 — `//`-operator comments, trailing attr `;`, `(*)` multiply value

1. **`//&&` comment lines** (FCS Symbols, FAKE MSBuildHelper — the "compound"
   mystery!): the leading-`/` continuation check treated `//` as an operator
   continuation, and the internal lexer then lexed `//&&` as symbolic_op (tie).
   Scanner: c0=='/' && c1=='/' is never an infix continuation. Grammar:
   line_comment got token prec 1, xml_doc_comment prec 2 — LEXICAL PREC
   OVERRIDES MATCH LENGTH in tree-sitter, so boosting `//` above symbolic_op
   silently broke `///` until the doc token was boosted above both.
2. **`[<A; B;>]`** trailing semicolon in attribute lists (Saturn benchmarks).
3. **`(*)`** is F#'s multiply-operator-as-value, not an empty comment: declined
   in finish_block_comment + both next_line_indent comment paths; the internal
   block_comment regex can't match `(*)` so `( `*` )` lex as separate tokens
   and operator_name's "*" branch parses it.

Bench: 195→191 files, 1127→1088 nodes, 0 regressions (ProvidedTypes +6 /
TypeTests +1 recovery reshuffle). MSBuildHelper 24→0, Symbols.fs 18→0.
Corpus 452.

## Fix 19 — when-constraint on a body-level type ascription

`… = Seq.max x : 'T when 'T : comparison` (FSharpPlus Foldable):
type_ascription_expression gained optional($._when_constraints).
Bench: 191→190 files, 1088→1073 nodes, 0 regressions (Collection.fs +2 =
reshuffle in its parked aligned-spaces site). Foldable.fs 15→0. Corpus 453.

---

## Session summary (2026-06-11 overnight)

**Bench: 275 → 190 failing files (3654 total = 94.8% clean), 3695 → 1073 error
nodes (−71%).** 19 commits, each gated on: corpus green, layout.fsx 0 errors,
full-bench sweep with zero new failing files. Corpus grew 428 → 453 tests;
layout.fsx gained L25–L33 plus several inline examples.

Remaining top (all triaged): ProvidedTypes.fs 230 (giant vendored TP file,
recovery-noise-dominated — own session), IlxGen.fs 52 (multi-line tuple
pattern in arms — passes in isolation, context-dependent), TypeNat 42 (parked
SRTP-as-arg), General.fs 31 (compound), fcs codegen/pars.fs 31 (fsyacc
long-lines), Collection.fs 29 (parked aligned-spaces), CheckExpressions 26
(parked `let Ctor(a,b), rest =` — two attempts broke fn-defs), Chocolatey 24
(compound pipe-match), NameResolution 17 (FSC "undentation" arm-body-at-arm-col
idiom — known hard).

The doc-comment attachment feature from last session remains in `stash@{0}`,
to be reviewed/applied separately (see memory project_bench2_sweep).

## Fix 20 — dotnet/fsharp onboarding: IL type args, static optimizations, `//`-newline lexer bug

New suite: dotnet/fsharp src/ (260 files; tests/ is ADVISORY ONLY — it contains
intentionally-invalid fixtures and never joins the regression gate). Baseline
was 48 failing / 1220 nodes, dominated by FSharp.Core prim-types.fs (825).

1. **Inline IL type arguments**: `(# "unbox.any !0" type ('T) x : 'T #)`.
2. **Static-optimization equations** (FSharp.Core-only syntax):
   `let inline f (x:'T) = body⏎ when 'T : bool = …⏎ when ^T : int32 and
   ^U : int32 = …` — new static_optimization rule repeated in
   _ascribable_body; scanner blocks LAYOUT_SEMI before `when`.
3. **line_comment newline bug**: the regex alternative /[^/].*/ matched the
   NEWLINE (negated classes match \n!), so an EMPTY `//` comment swallowed the
   whole next line. Latent forever; exposed when prec-boosting made the regex
   win more ties.

prim-types.fs 825→17. fsharp-src: 48→43 files / 1220→363 nodes. 23-repo:
190→187 / 1073→1035, 0 regressions both (fsi.fs +8/+7 in both = recovery
reshuffle around a PRE-EXISTING `do x <- if…else` site — verified by A/B
stash). Corpus 455, layout L34.

## Fix 21 — mid-line `else` may only close THEN/ELIF bodies

`do x <- if c then Some AMD64 else Some X86` (fsi.fs, both copies): the do-body
is an inline S_EXPR, so the mid-line else close (fix 4's rule) popped it too —
the else detached from the inner if SILENTLY (the "passing" variant produced a
garbage tree with `else` as an identifier, zero ERROR nodes — count-clean ≠
correct!). New `_then_open` external (appended at externals END) gives then/
elif bodies a `thn` Ctx flag; the mid-line else/elif close now requires
`top->inl && top->thn`. do/lambda/while/else bodies keep their else for the
inner if.

23-repo: 187→186 / 1035→1013; fsharp-src: 43→42 / 363→345; 0 regressions.
fsi.fs 18→0 in BOTH suites. Corpus 456, layout L35.

## Fix 22 — computed enum values + IL array type definitions

`| CustomComparisonAttribute = (1uL <<< 9)` (FCS flag enums — WellKnownAttribs
99→0, il.fs 27→0): enum_case values accept parenthesized_expression.
`type ``[,]``<'T> = (# "!0[0 ...,0 ...]" #)` (prim-types-prelude 21→0):
inline_il_expression as a type-alias body.

fsharp-src: 42→39 files / 345→198 nodes; 23-repo unchanged (1013), 0
regressions. Corpus 457, layout L36.

## Fix 23 — F# 9 nullness annotations + subtype constraints in generic args

`seq<'U :> seq<'T>>` (seqcore), `outputDir: string | null` in member params
(tuple_typed_pattern's type slot), `: string | null * IDep | null =` nullable
TUPLE returns (dedicated nullable_tuple_type — must contain ≥1 nullable element;
extending the general tuple_type conflicts with `|` starting the next DU case
after a labelled union field), and typed_expression accepts nullable too.
PARKED: `DefaultParameterValue(null: string | null)` — attribute args use a
separate restricted expression path (3 nodes, DependencyProvider).

23-repo: 185/1006; fsharp-src: 36/178; 0 regressions. Corpus 458.

## Fix 24 — default SRTP constraint, nullable `:>` RHS, member-val ascription

`and default ^Value : float` (FSharp.Core Query), `'Resource :> IDisposable |
null` (tasks.fs), `member val Items = [||]: ITaskItem[] with get, set`
(FSharp.Build). fsharp-src: 36→32 files / 178→164 nodes; 23-repo unchanged;
0 regressions. Corpus 459, layout L37.

---

## Session summary #2 (2026-06-11 daytime, autonomous continuation)

Fixes 20–24, triggered by onboarding **dotnet/fsharp** as a new suite:
- `src/` (260 files) joined the regression gate: baseline **48 failing / 1220
  nodes → 32 / 164 (87.7% clean, −87% nodes)**.
- `tests/` (5452 files) is ADVISORY ONLY — it contains intentionally-invalid
  compiler-test fixtures and must never gate.
- 23-repo suite over the same fixes: 187→185 files / 1035→1006 nodes.

Notable: the `//`-swallows-next-line lexer bug (negated regex classes match
newline!), FSharp.Core's static-optimization equations + inline-IL type args,
the `_then_open` external (mid-line else closes ONLY then/elif bodies — the
broken `do x <- if…else…` parse had ZERO error nodes; clean counts are not
proof of correct trees), computed enum flags, and first-class F# 9 nullness
(`string | null`) in params/returns/constraints.

Remaining fsharp-src top: IlxGen 54 (multi-line tuple arm patterns), prim-types
17 (GADT-style case signatures + `( :: )` member access), NameResolution 17
(undentation idiom) — all triaged, all known-hard.

## Fix 25 — gaps found by comparing against ionide/tree-sitter-fsharp

Cross-parser comparison surfaced files ionide parses and we didn't:
1. **Copy-update with a dot-expression base**: `{ X.Default("A").Settings with
   CopyLocal = Some true }` — dot_expression added to all four record/anon
   copy-update base slots (Paket BasicScenarioSpecs 12→0).
2. **Ctor attrs with trailing comments**: `[<ParamObject>] // …⏎ [<Emit>] // …⏎
   (params) =` — CTOR_ATTR probe skips `//` comments and repeated attr rows
   (Feliz POJO docs, both clones).
3. **`lazy` block bodies**: `lazy⏎    assert …⏎⏎    match …` — new layout
   branch via a DEDICATED `_lazy_open` external that DECLINES inline bodies.
   Three failed designs: _expr_open stole `lazy b` match scrutinees
   (`match a, lazy b with` — Map.fs); _block_open's S_DECL blew up FxResolver
   41 nodes; ascription had to be allowed inside (`= lazy x.Value : Lazy<'T>`,
   Monad.fs).
4. **REVERTED**: struct anonymous records `struct {| … |}` — both encodings
   (optional("struct") inline; separate wrapper rules) broke `struct (a, b)`
   TUPLES via silent GLR mis-resolution (tasks.fs 19, ArrayTests×5,
   DecoderCE 8). Needs a dedicated session with explicit conflicts; PARKED.

23-repo: 185→179 files / 1006→975 nodes; fsharp-src: 32→31 / 164→159;
0 regressions. Corpus 460.

MEASUREMENT WARNING #2: ionide's grammar is ALSO named "fsharp" — the
tree-sitter cache keys by grammar NAME, so parsing from OUR cwd after building
theirs served THEIR parser (root node `(file` instead of `(source_file` was
the tell). `rm -rf ~/.cache/tree-sitter` + re-warm before every parser switch.

## Fix 26 — struct anonymous records (the parked one, solved)

`struct {|A: int|}` / `struct {|C = 1|}` via separate wrapper rules wired at
all SIX anon-record positions (_expression, _simple_expression, _dot_object +
3 type lists). ROOT CAUSE of both earlier failures finally identified:
struct_tuple_expression was never in _simple_expression — once `struct` became
shiftable in argument position via the anon wrapper, the parser committed there
and the tuple's old (indirect) route was preempted. The fix is CO-LOCATION:
struct_tuple_expression added beside the wrapper in _simple_expression. A
conflicts declaration was NOT needed (the generator even flags it unnecessary)
— the lesson is that a keyword shared between rules must be shiftable toward
ALL of them from every state where ANY of them is reachable.

23-repo: 179→176 files / 975→951 nodes (3 AnonRecordTests copies), 0
regressions. Corpus 461.

## Fix 27 — mid-line `|?>`-style operators inside match arms (NameResolution 17→0 ×2)

The "undentation" item dissolved on inspection: the minimal undentation idiom
already parses (earlier arm fixes covered it). NameResolution's real residue
was the MID-LINE twin of fix 3: the bare-`|`-is-an-arm close in the midline
closer path only excluded `|>`/`||`, so `t1 |?> List.choose (function …)`
inside a match arm closed the arm body at the `|?>`. Now `|` + ANY operator
char is infix there too. Both NameResolution copies 17→0.

23-repo: 176→175 / 951→934; fsharp-src: 31→30 / 159→142; 0 regressions.
Corpus 462.

## Fix 28 — pipeline through a match (the Chocolatey compound, solved)

`builder |> match … with⏎ | arm -> …⏎ |> next (args on continuation lines)`:
three coordinated scanner changes —
1. The leading-infix continuation now mirrors FSC's offside GRACE: an op
   dedented more than ~(token+1) below an S_LAYOUT body column is offside and
   CLOSES the body (previously S_LAYOUT bodies never closed for leading ops,
   so the dedented `|>` extended the LAST ARM and the next pipe's continuation
   ARGUMENT lines mis-lexed as arm patterns).
2. S_MATCH: an op at OR below the arm column ends the arm list first
   (`≤`, so `| false -> b⏎|> g` at the arm col pipes the whole match).
3. bar classification unified: `|` + any operator char is never an arm marker,
   peeked ONCE where bar_arm is computed (the S_MATCH close previously saw
   `|>` as a new arm via first=='|').
Two corpus expected-trees updated — the new shape `binary_expression(match, g)`
is the F#-correct attachment (the old trees nested the pipe inside the arm).

23-repo: 175→174 files / 934→910 nodes (Chocolatey 24→0), fsharp-src
unchanged, 0 regressions. Corpus 463.

---

## Session summary #3 (2026-06-11, dedicated parked-items session)

All three targets landed:
1. **`struct {| … |}` anonymous records** (fix 26) — root cause of both earlier
   failures: struct_tuple_expression was missing from _simple_expression, so
   making `struct` shiftable there via the anon wrapper starved the tuple.
   CO-LOCATION was the fix; no conflicts declaration needed.
2. **The "undentation" item DISSOLVED** (fix 27) — the idiom already parses;
   NameResolution's real residue was the mid-line twin of the `|?>` bug.
   17→0 in BOTH suites.
3. **Chocolatey pipe-match compound** (fix 28) — FSC-style offside grace for
   leading operators below S_LAYOUT bodies + arm-column ops end the arm list +
   unified `|`-operator classification. 24→0.

**Standings: 23-repo 174 failing / 910 nodes (95.2% clean; session start
3695 nodes); fsharp-src 30 / 142 (88.5% clean; baseline 1220 nodes).**

Investigated and left: IlxGen (52/54 both suites — resists synthetic and
slice reproduction; window/prefix cuts kept producing truncation artifacts:
REMINDER that a slice ending in a trailing inner `let` with no continuation
is INVALID F# and errors correctly); General.fs (29, compound, prefix-bisect
non-monotonic); ProvidedTypes (230, own session); TypeNat/Collection (parked
SRTP/aligned-spaces).

## Fix 29 — spec-battery gaps: measure juxtaposition, wildcard slices, nameof<T>

A systematic F# language-reference battery (60+ probes over bindings, types,
patterns, CEs, OO, strings, literals, quotations, F# 7/8/9 features) found
only THREE parse gaps — none of which appear in either bench suite:
`float<m s^-2>` (juxtaposed measure product, spec 9.5), `m[*, 2]`
(whole-dimension slice), `nameof<T>` (explicit type-arg form). All fixed.
Everything else in the battery — IWSAM, `_.Name` shorthand, `while!`,
string interpolation variants, active patterns, byref/inref/outref, fixed,
events, indexers, delegates, quotation splices, computed ranges — parses.

Suites unchanged (the gaps were bench-absent; prim-types ±1 recovery wobble).
Corpus 464.

## Fix 30 — review-table items 7+8 (ctor-tuple destructuring + ascribed attr args)

1. **`let CheckedBindingInfo(a, b, …), tpenv = …`** (the twice-failed item) —
   cracked with the SCANNER-GATE pattern (CTOR_ATTR's trick): a zero-width
   `_ctor_tuple_gate` emitted only when `ident(.ident)* ( …balanced… ) ,`
   genuinely follows (fn defs never have `,` after params, so the gate is
   deterministic and the fn-def LR path is untouched). The conflict was
   invisible to `conflicts:` declarations because hidden-rule inlining merges
   the items. First placement (early in the scan) regressed FParsec's
   split-binder files (`#if LOW_TRUST⏎ let⏎#else⏎ use⏎#endif`) by
   short-circuiting the preproc-region emission — moved to the consumption-safe
   MID-LINE TAIL, resuming after the word-branch's partial identifier consume.
2. **`DefaultParameterValue(null: string | null)`** — parenthesised attribute
   args accept an ascribed form whose type may be nullable
   (alias → type_ascription_expression).

23-repo: 174→172 / 910→905; fsharp-src: 30→28 / 143→138; 0 regressions.
DependencyProvider 3→0 and CheckExpressions 2→0 in BOTH suites. Corpus 466.

## .fsi (signature files) assessment — measured, not implemented

Swept all 213 .fsi files in dotnet/fsharp src/ with the current grammar:
**212/213 fail, 14 097 error nodes over 72k lines (~1 per 5 lines)** — i.e.
signature files are effectively unsupported today, as expected (they were
deliberately excluded from every suite).

What's missing (probed): module-level `val [inline] name: type` declarations;
bodiless member signatures (`member X: int`, `new: x: int * y: int -> Point`,
`static member Origin: Point`); and the hard one — NAMED parameter segments in
curried function types (`val map: f: ('a -> 'b) -> list: 'a list -> 'b list`),
which need labelled-arrow type forms that may interact with ascription/record
field parsing.

What already exists and can be reused: abstract_member_defn (same shape as
member sigs), val_field, the whole type_expression machinery.

Effort estimate: ONE dedicated session in the style of this branch —
(a) val_signature + member-signature rules + labelled function types,
(b) gate on the ready-made 213-file fsi suite plus the two existing suites
(zero-regression on .fs is the hard requirement),
(c) realistic landing point 90 %+ fsi files.
Alternative if grammar conflicts prove nasty: a separate `fsharp_signature`
grammar in this repo (ionide's approach — two grammars sharing the scanner),
at the cost of duplicate maintenance. Recommendation: try in-grammar first.

## Fix 31 — DESIGN PIVOT (user decision): both `#if/#else` branches parse as code

Review feedback: the inactive-branch-as-trivia model (fix 1) grays out the
whole `#else` branch — unacceptable for Fable-style dual-path projects where
that branch is full-sized real code. Reverted to both-branches-as-code:
`preproc_elif`/`preproc_else_kw` restored as extras, the directive LINES skip
in geometry, `preproc_inactive` stays DECLARED (extern enum stability) but is
never emitted. The original fix-1 motivation (consecutive functions with
#if-guarded arms) now parses fine as-code — the intervening scanner fixes
(over-indented arms, |-classification, then/lazy gating) had solved the real
geometry bugs.

Measured cost of the pivot, accepted by review: 23-repo 172→197 files /
905→1023 nodes; fsharp-src 28→29 / 138→159. ALL regressions are the
"alternative-header splice" class — duplicate ctor headers (`(paramsA) =` /
`(paramsB) =` per branch, prim-parsing), member-signature splices (Thoth
Decode.Auto), unbalanced parens across branches (`(` vs `lock … (fun () ->`,
range.fs), keyword splices (FParsec let/use) — shapes that CANNOT compose as
sequential code by construction. A repeat-primary-ctor liberalization was
tried and reverted (the spliced headers each carry their own `=`).
In exchange, every dual-branch `#else` in user code colors fully.

Corpus 466 (4 preproc trees re-shaped to dual-code), layout.fsx L25 updated.

## Fix 32 — doc-comment attachment merged from the stash (+ known gap CLOSED)

The stashed doc-attachment feature (docs become CHILDREN of their declarations
so Helix expand-selection walks doc → decl → scope) applied onto the new main
with only 4 conflict hunks: let_and_binding (stash's `_and_docs` + our
`repeat($.attribute)` — both kept), scanner globals block (both sides kept),
and two corpus hunks (both tests kept). Git auto-merged the externals/enum
insertions consistently (AND_DOCS_OPEN/CASE_DOCS_OPEN at index 23/24 — 33/33
alignment verified). Generated files regenerated rather than merged. Two
stash-era conflict declarations had become unnecessary and were dropped.

BONUS: the stash's documented known gap — docs between a type NAME and its
primary ctor (`type StringSyntaxAttribute⏎ ///<param …>⏎ (syntax, …) =`,
4 vendored Feliz files; three mechanisms failed in the stash era) — is now
CLOSED by generalizing fix 25's CTOR_ATTR probe: any mix of attr rows and
//-or-/// lines may precede the ctor `(`, requiring ≥1 such row so plain
ctors keep their ungated path.

23-repo: 197→190 files / 1023→1010 nodes (7 FAKE-legacy files fixed by
attachment), fsharp-src unchanged, ZERO regressions. Corpus 467 (the stash's
doc test included), layout.fsx gained L24_DocComments.

## Fix 33 — two-step expand-selection for documented declarations

Review feedback: from inside `let multiDoc = 1` under a `///` block,
expand-selection jumped straight to docs+let. Wanted: code first, then docs.

The docs were direct children of the declaration node, so no intermediate
"code-only" node existed. Now every doc-bearing rule (let_binding, type_decl,
member_defn, abstract_member_defn, module_decl, exception_decl,
type_extension, interface_impl, union_case, enum_case, record_type_field,
let_and_binding, _and_decl_indented, type_and_decl, val_field,
secondary_constructor — 16 wrappers) nests the code part as an inner
SAME-NAMED node under a `decl:` field when docs are present:

    (let_binding (xml_doc_comment)+ decl: (let_binding name: … body: …))

Without docs the hidden core's children splice in flat — tree unchanged.
Expansion: code → code+docs → scope. Query fallout: indents.scm patterns that
matched field-less nodes (`!body`, bare node) gained `!decl` so the docs
wrapper never double-indents; field-anchored highlights match the inner node
unchanged; locals' doubled @local.scope is harmless. Conflict sets renamed to
the `_core` rules; one set is mislabeled "unnecessary" by the generator but
required (comment in grammar.js).

Suites: 23-repo byte-identical (190/1010); fsharp-src 29/160 (±1 recovery
wobble in prim-types' known GADT residue). Corpus 467.

## Fix 34 — documented union cases / and-clauses start AT their docs

Review feedback: expanding from `float` in a documented union case gave
case → (a node starting right after the `=`) instead of case → docs+case.
Cause: the scanner-gated doc markers (CASE_DOCS_OPEN / AND_DOCS_OPEN) are
zero-width and anchored at the scan baseline — the END of the PREVIOUS line —
so the docs+case wrapper's extent began there.

Attempt 1 (de-gate the wrappers so real doc tokens anchor the node) was
REVERTED: a doc after the LAST case shifts into a phantom next case, exactly
as the stash-era post-mortem warned; conflict declarations don't help because
the doc-shift state lacks the sibling items.

Landed design keeps the gates and fixes the anchor in two coordinated parts:
1. next_line_indent (MAIN boundary call) moves the zero-width baseline to the
   first `///`/`//` line's start — gates AND closes emitted by that scan
   anchor at the doc block.
2. Because the scan then RESUMES mid-line ON the docs, a new MID-LINE
   doc-resume dispatch skips the doc block (+ blank lines) forward and runs
   the same try_and_docs dispatch (gates, typebody-close-at-docs) from there;
   on no-match the parser just lexes the doc itself. (Without part 2 the
   resumed scan never reached the boundary dispatch and the and-clause
   detached — the trap that sank the first anchoring attempt.)

Extents now: outer union_case = [doc-line .. case-end]; type_and_decl =
[doc-line .. clause-end]. Expansion: float → case → docs+case → cases → type.
Suites: 23-repo 190/989 (−21 nodes, recovery improvements), fsharp-src
identical; 0 regressions. Corpus 467.

## Fix 35 — tight extents: declarations no longer bleed into the next doc block

Review screenshots showed every declaration FOLLOWED by a documented one
over-extending across the blank line to the next `///` line's start —
fix 34's doc anchoring moved the baseline for ALL zero-width tokens of a
doc-skipping scan, including the LAYOUT_END that closes the PREVIOUS binding
(the close's anchor IS the closed node's end).

The anchor is now taken only when a doc gate can actually fire:
1. `g_doc_gate_possible` — CASE_DOCS_OPEN or AND_DOCS_OPEN is valid in this
   scan (a let-body close scan has neither: the and-clause item only appears
   AFTER the body reduces, so ordinary closes keep the tight old baseline);
2. `indent >= top->col` — docs at/right of the layout column decorate a
   case/and AT that level; docs LEFT of it belong to a declaration after a
   dedent-close (`type … | Point⏎⏎/// next⏎let …`), which must close tight.

Extents now: `let multiDoc = 1` ends at the `1`; `type Shape` ends at
`| Point`; documented cases/and-clauses still START at their docs.
Suites: 23-repo 190 files (ProvidedTypes ±21 = its usual recovery
oscillation), fsharp-src identical; corpus 467, layout 0.
