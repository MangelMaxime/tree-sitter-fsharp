# F# offside scanner — uniform-layout rewrite (spike)

Branch: `spike/layout-scanner`. Goal: replace the 24 special-cased offside externals
(7 stack "kinds", per-construct OPEN/SEP/CLOSE triples that the scanner *guesses* between)
with a single uniform layout model, ported from the principles of tree-sitter-haskell.

## Why

Every remaining real-world parse failure (Fable2Babel 69, Pipeline 16) is an
**accumulated-offside-state** bug: the scanner *guesses* which construct opens/closes at a
boundary from column heuristics across 7 interacting kinds. The guesses are individually
patchable (we fixed 4 this week) but the failures that remain are emergent from long
statement sequences and have **no minimal repro** — they aren't isolable, so they aren't
grindable. The root cause is architectural: **the scanner decides when to open a context.**

## The model (from tree-sitter-haskell)

Haskell's layout scanner has three behavioural classes of external symbol:

- **`cmd_*` (commands)** — emitted by the GRAMMAR right after a token that unconditionally
  starts a layout (`{`, `do`, `of`, `let`, …). The parser has already committed; the command
  just tells the scanner to push a context. *The grammar decides opening, not the scanner.*
- **`cond_*` (conditions)** — scanner-decided from state + lookahead: the layout semicolon
  (separator at equal indent) and the layout end (close at lesser indent). Crucially these
  are **gated on `valid(END)`** — the scanner only closes when the grammar (or a parse error)
  permits it. The scanner never decides *which* construct, only *whether* to close here.
- **`error_sentinel`** — an unused symbol, valid only when tree-sitter marks *all* symbols
  valid (= parse-error recovery). Clean replacement for our `nvalid > 20` hack.

`newline_process` order per boundary: `end_layout_indent` (pop one ctx if `indent < ctx.indent`
&& `valid(END)`) → inline enders (`in`/`else`/…) → open any pending layout → `semicolon`
(if `indent <= ctx.indent`). Context = `{sort, indent}` (same shape as our `{kind, col}`).

The win: **one** close/separate algorithm over **one** stack. The cross-kind interactions
that produce accumulated-state bugs cannot exist because there is only one kind of decision.

## F# layout contexts (sorts)

Grouped by distinct closing behaviour:

| Sort        | Opened after (grammar cmd)                          | Separator | Closes on |
|-------------|-----------------------------------------------------|-----------|-----------|
| `Decl`      | `=` (let/member/module), top-level, `let_decl` body | semicolon | dedent; parse-error |
| `Then`      | `then` / `else` body                                | semicolon | dedent; `else`/`elif`; parse-error |
| `Do`        | `do`/`while…do`/`for…do` body                        | semicolon | dedent; parse-error |
| `Match`     | `with` (match/try) / `function` / lambda `->`       | `|` only  | dedent below arm col; `with`/`finally`; parse-error |
| `Let`       | `let … =` *when an `in` may follow*                  | semicolon | `in`; dedent; parse-error |
| `Brackets`  | `[` `[|` `{` (record/list/array/CE) — explicit close | dedicated semi | `]`/`|]`/`}`; (no indent close) |

Notes:
- The current **inline vs own-line** split (`_body_indent` vs `_let_body_open`, etc.) DISAPPEARS:
  a context's `indent` is simply the column of the body's first token, same-line or next-line.
  `let x = e` and `let x =\n    e` push the same `Decl` ctx at e's column.
- `Match` is Haskell's `MultiWayIfLayout` analogue: no layout semicolons; arms delimited by `|`.
  Our `_match_arm_sep` + the column guard we just added both fold into "close `Match` when a
  `|`/token dedents below the arm column, gated by valid(END)".
- `Brackets` keeps a **dedicated** separator (today's `_bracket_sep`/`_ce_sep`/`_record_field_semi`)
  so a nested sequence can't steal it — preserved as one `cond_bracket_semi`.

## New external set (target ~9, was 24)

```
error_sentinel
cmd_open_decl  cmd_open_do  cmd_open_then  cmd_open_match  cmd_open_let  cmd_open_brackets
cond_semi          // layout semicolon (Decl/Then/Do/Let)
cond_bracket_semi  // dedicated separator inside Brackets
cond_end           // close innermost layout (all sorts; gated valid + indent/inline-ender)
cond_end_bracket   // close an explicit Brackets ctx (aliased to ']'/'}' shaping if needed)
```
(`_float_trailing_dot` is lexical, unrelated to layout — keep verbatim.)

## Grammar transformation (mechanical, per body-shape rule)

Every `choice(seq(_body_indent,e,_body_dedent), seq(_X_open,e,_X_close), e)` becomes:
```
seq($._cmd_open_<sort>, e, $._cond_end)
```
emitted immediately after the layout keyword (`=`/`then`/`->`/`do`/`with`). Sites:
`let_binding` (1550), `_method_body` (674), `_indented_or_inline_body` (1459, if/for/while/lambda),
`_match_arm_body` (2108), `for_expression` (1909), records/lists (1381…), `computation_expression`
(1960). The `_match_arms` repeat keeps `|` but its separator/close come from `Match` rules.

## Risks / open questions (validate in the spike)

1. **Records/lists/CE** are the fiddliest today (dedicated seps, `{ F1\n F2 }` field column). Port
   them as `Brackets` with `cond_bracket_semi`; verify the corpus's newline-record tests.
2. **`with` is overloaded**: match/try arms vs `type … with` augmentation (our `_type_augment_dedent`)
   vs record copy `{ r with … }`. The cmd is emitted by the grammar at each distinct `with`, so this
   is actually *easier* than today (no scanner disambiguation).
3. **Mid-edit incomplete input** (`let x =`, `if c then`): NICE-TO-HAVE per user, not a gate. With
   grammar-driven opens, an unfinished body leaves a pushed ctx that the next dedent/parse-error closes
   — likely still works, but drop the dedicated incomplete branches if they fight the model.
4. **`prec.dynamic` attribute-attachment** machinery is independent of layout — leave untouched.

## Gate (RELAXED per user 2026-06-06)

The 312 corpus is **no longer a hard must-pass** — the user has OK'd breaking/updating
corpus tests to make progress (the fsharp-helix grammar isn't public; cleaner model wins,
see [[feedback_grammar_test_updates]]). Target instead:
- `examples/layout.fsx` parses cleanly (the new ordered harness, grown construct-by-construct).
- the `dd` accumulated-state repro = 0, and the 8 tmp/fable files improve.
- corpus regressions are reviewed and either fixed or the corpus entry updated to the new
  (cleaner) tree — NOT silently ignored.

If the model genuinely can't express F# offside without exploding, stop, keep the 4 banked
fixes on `main`, and document why.

## RESUME STATE (2026-06-06)

- Branch `spike/layout-scanner` (off `main` @ b297451, which has all 4 banked fixes).
- `examples/layout.fsx` created = ordered simple→complex, sub-module-organised harness
  (validates clean on the CURRENT grammar). This is the construct-by-construct test target.
- Reference repo cloned at `/tmp/ts-haskell` (re-clone if gone:
  `git clone --depth 1 https://github.com/tree-sitter/tree-sitter-haskell /tmp/ts-haskell`).
- NEXT: Step 1 skeleton scanner, then Step 2 Decl sort end-to-end (see Spike plan below).
- Approach decided: TRANSFORM the existing grammar.js in place (keep the ~90% non-layout
  rules), swapping only the externals + the ~8 body-shape sites to `seq(cmd_open_<sort>, e,
  cond_end)`. Do NOT rewrite the grammar from scratch.

## PoC RESULTS (2026-06-06) — MODEL VALIDATED ✅

Built an isolated tree-sitter PoC (`spike/poc/`, ~90-line scanner + 30-line grammar) with
just `error_sentinel`/`_open`/`_semi`/`_end` and a grammar of nested `let … = <body>` +
`if/then/else`. All the cases that defeat the current scanner pass:

| case | result |
|------|--------|
| flat sequence (`_semi` at equal indent) | ✅ |
| **nested multi-level dedent** (`2`@8 closes deep, `3`@4 closes inner, sibling@0 closes outer) | ✅ correct nesting |
| if/then/else with own-line branches | ✅ both branches captured |
| nested if/then/else | ✅ |
| parse-error recovery (`= = =` mid-file) | ✅ no cascade, no hang, good decls survive |
| same-indent `f a` / `g b` = two statements | ✅ |
| more-indent continuation `f`⏎`  a`⏎`  b` = one app | ⚠️ scanner emits the right thing (NO token); toy `app` rule mis-chains — a grammar issue, not a model limit |

**The multi-level `valid(END)`-gated dedent — the exact thing our accumulated-state bugs
fail — works.** That was the make-or-break unknown. Go.

### Implementation lessons (apply these in the real port)

1. **`lexer->mark_end(lexer)` at the TOP of scan**, before any lookahead `advance(skip=true)`.
   Without it, a zero-width token *consumes* the newline/indent it peeked, so a second
   layout token at the SAME boundary (END-then-SEMI, or multi-level END) can't see the
   newline and never fires. (Our real scanner already does this at line 589.)
2. **`_open` is peeked by the scanner**: skip horizontal ws; if at newline use
   `next_line_indent` for the column, else `get_column`. Push that column. Same-line and
   own-line bodies become identical — the inline/own-line distinction is gone.
3. **Multi-level close = one END per scan call**; tree-sitter re-invokes at the same
   (mark_end-restored) position until the scanner stops returning tokens. Pop one ctx per call.
4. **Recovery**: `if (valid[error_sentinel]) return false;` — stay out of tree-sitter's way.
   Simpler than the Haskell parse-error(t) close and empirically recovers without cascade.
   (May refine later to allow END-on-error if a real case needs it.)
5. **Layout tokens only fire when a newline separates** the current position from the next
   token: skip horizontal ws, require lookahead == newline/EOF, else `return false`.
6. **The `app`/multi-line-application ambiguity** (more-indented continuation vs sequence) is
   the one genuinely fiddly area — the scanner does the right thing (no token on >indent), but
   the grammar's application rule must accept the continuation. This is where the real port's
   GLR conflicts / non-stealable separators will need care (today's `_bracket_sep` discipline).

## REAL PORT — WIP STATUS (2026-06-06, transform APPLIED & generating)

The transform below has been APPLIED on `spike/layout-scanner`. Current state:
- **`tree-sitter generate` succeeds**; new scanner wired (`src/scanner.c`, mirror in `scanner_new.c`).
- **`examples/layout.fsx`: 19 errors** (from 23 at first generate). **Corpus: 266/312** (46 fail —
  pre-approved to regress during the rewrite; triage at the end).
- Fixes landed this session, in the scanner: (1) **mid-line close** — emit LAYOUT_END/MATCH_END/
  BRACKET_CLOSE before a same-line `)`/`]`/`}` (fixes `(fun x -> body)`, match-in-parens);
  (2) **`semi_blocked`** — no LAYOUT_SEMI before closers `)]}`, `|`, or continuation keywords
  (else/elif/then/with/finally/in/and); in the grammar: (3) removed the **bare-`_expression`**
  alternative from `_indented_or_inline_body` and `_match_arm_body` (it let an inline body reduce
  WITHOUT a layout close → `if a then b`⏎`else …` dropped the else). Also collapsed the redundant
  `_type_decl_body_aug` (the old `_type_augment_dedent` path) into `_type_decl_body_or_class`.

### REMAINING errors (next session — debug in this order)
1. **`let_binding` in-form bare-`_expression`** still present (line ~1574 `field('body', $._expression)`)
   — kept because `let x = e in body` needs `in` to close the Let body, which the scanner does NOT
   yet do. **Add an `in` inline-ender for S_LAYOUT** (mirror `semi_blocked`'s word peek: when the
   layout body is followed by `in`, emit LAYOUT_END), then remove the bare branch. This is the **Let
   sort** edge case from the sorts table.
2. **Records / braces `{ … }`** (L11): the brace-body builder (grammar.js ~93-121) still has BOTH a
   `_bracket_open` form (form 1) AND a `_layout_open` form (form 2) plus inline (form 3). After `{`,
   the scanner emits LAYOUT_OPEN first (unconditional) → spurious context for some brace forms.
   **Fix: drop the `_layout_open` (form 2) brace alternative; let `_bracket_open` handle all block
   braces.** But `_bracket_open` only fires on newline-after-`{`; the `{ F1`⏎`F2 }` form (F1 on the
   `{` line) needs bracket_open to fire there too → may need a brace-specific always-open, OR accept
   form 3 (inline `;`) extended to multiline. Records are THE fiddly case (flagged from the start).
   Note: isolated `{ X = 1; Y = 2 }` and `type Point = { … }` parse CLEAN; the L11 failure is a
   specific own-line/copy form or an L10 (`[ (sideEffect (); value); other ]`) cascade — triage both.
3. **Computation expressions** (L12): CE body is `S_BRACKET` (`{`…`}`); object expressions
   `{ new IFoo with member … }` (use-binding value) and `use`+`do!` need re-checking. Re-verify the
   4 banked fixes' behaviour survives.
4. **Cascades** (L13 DU, etc.): isolated-clean constructs erroring in-file ⇒ an earlier module leaves
   a context open. Fixing 1-3 likely clears these.

### Corpus triage (after layout.fsx is green)
46 failures — bucket them; most should fall out of fixes 1-3. Genuinely-changed trees (cleaner under
the new model) → update the corpus `.txt`. Keep node names stable for queries.

## REAL PORT — TRANSFORM SPEC (applied; kept for reference)

**Status: new scanner written & compiles** → `src/scanner_new.c` (10 externals, all 3 sorts,
reuses `next_line_indent`/`scan_trailing_dot_float`; based on the validated PoC). To wire:
`cp src/scanner_new.c src/scanner.c` then transform grammar.js below, then `tree-sitter generate`.
(Backups: `git checkout grammar.js src/scanner.c` restores the working version anytime.)

### New `externals:` block (order MUST match the scanner enum)
```js
externals: $ => [
    $._error_sentinel,   // unused in rules; valid only on all-symbols-valid (recovery)
    $._layout_open,      // generic body open (Decl/Then/Do/Let) — after =/then/else/->/do
    $._layout_semi,      // generic separator (next line == body col)
    $._layout_end,       // generic close (next line < body col)
    $._match_open,       // arm-list open — after with/function/(lambda)->
    $._match_end,        // arm-list close (dedent below arm col, or == col & not `|`)
    $._bracket_open,     // [ / [| / { block body on its own line(s)
    $._bracket_semi,     // newline-aligned element/field separator
    $._bracket_close,    // ] / |] / } closing a block bracket
    $._float_trailing_dot,
],
```
NOTE: `_error_sentinel` must NOT appear in any rule (that's what makes it the recovery flag).

### Site mapping (old → new) — apply to every site from the grep
| OLD pattern | NEW |
|---|---|
| `seq(_body_indent, e, _body_dedent)` | `seq($._layout_open, e, $._layout_end)` |
| `seq(_indent, e, _dedent)` | `seq($._layout_open, e, $._layout_end)` |
| `seq(_inline_open, e, _inline_close)` | `seq($._layout_open, e, $._layout_end)` |
| `seq(_let_body_open, e, _let_body_close)` | `seq($._layout_open, e, $._layout_end)` |
| `seq(_for_body_open, e, _for_body_close)` | `seq($._layout_open, e, $._layout_end)` |
| `_virtual_semi` | `$._layout_semi` |
| `choice(";", _virtual_semi)` | `choice(";", $._layout_semi)` |
| `_bracket_open/_bracket_sep/_bracket_close` | `_bracket_open/_bracket_semi/_bracket_close` (sep→semi) |
| `_record_body_open/_record_field_semi/_record_body_close` | `_bracket_open/_bracket_semi/_bracket_close` |
| `_ce_body_open/_ce_sep/_ce_body_close` | `_bracket_open/_bracket_semi/_bracket_close` |

The `let_binding` / `let_and_binding` / `let_decl_indented` / `_indented_or_inline_body` /
`_method_body` bodies all collapse their 2-3-way `choice(...)` to a SINGLE
`seq($._layout_open, field('body', $._expression), $._layout_end)`.

### Match (the one structural change)
- `_match_arm_body` (2108): `choice(seq(body_indent,e,body_dedent), seq(match_body_open,e,match_body_close), e)`
  → `seq($._layout_open, e, $._layout_end)` (arm body is just a generic layout).
- `_match_arms` (1839/1850): wrap the arm list:
  `seq($._match_open, $.match_arm, repeat($.match_arm), $._match_end)` — DROP `optional(_match_arm_sep)`
  (the `|` lives in `match_arm`'s `optional("|")`; arm-list close is `_match_end`).
  Apply to all arm-list producers: `match_expression`, `function`, `try…with`, `match!`.

### Known edge cases to handle during debug (don't forget)
1. **`type … with` augmentation** (old `_type_augment_dedent`, sites ~533/538): the type body is
   `_layout`; a `with` at the SAME column as the body won't trigger `_layout_end` (needs `<`). Options:
   treat `with` as an inline-ender for `S_LAYOUT` (close the layout when `first` two chars are `with`),
   mirroring how the PoC could close on `else`. Add a small `peek_with` to the scanner if needed.
2. **match-in-parens** (old mid-line MATCH_BODY_CLOSE before `)`): an inline arm body must close before
   a mid-line `)` (see `examples/layout.fsx` L07 `mapMatch` / simple.fsx `MatchLambdaParen`). The
   scanner's mid-line section may need a MATCH_END/LAYOUT_END before `)`. Watch this case.
3. **`|]` / `|}`** vs match `|` arm: at a line boundary `first=='|'` is ambiguous; the BRACKET_CLOSE
   branch (gated valid + top is S_BRACKET) handles array/anon-record close; the S_MATCH branch treats
   `|` as an arm. Verify arrays-of-unions don't confuse it.
4. **multi-line application** (`f`⏎`  a`): the scanner emits no token at `col > body_col`; the grammar's
   `application_expression` must accept the continuation (it does today). Watch L02 `piped`.
5. **`do`-not-`do!`** and the CE separators: CE bodies become `S_BRACKET` (`{`…`}`) with `_bracket_semi`;
   re-verify the 4 banked fixes' behaviour (CE if/else, use+do!) survives — they should, for free.

### Debug order (against examples/layout.fsx, module by module)
L01→L02 (Decl inline/own-line) → L03/L04 (functions/sequences) → L05 (if) → L06/L07 (match/lambda)
→ L08 (let-in) → L09 (loops) → L10/L11/L12 (brackets/records/CE) → L13-L16 → L17 (accumulated) →
then `dd` repro + the 8 tmp/fable files. Corpus: review regressions, update entries to the new tree.

## Spike plan (incremental, steady)

1. **Skeleton scanner**: context stack, `error_sentinel`, newline indent capture, the
   `end/semicolon` core gated on `valid()`. Reuse existing comment/string/number lexing verbatim.
2. **One sort end-to-end**: wire `Decl` only (let/member/module/top-level) in grammar+scanner;
   get a trivial file parsing. Prove the open-by-grammar / close-by-valid loop.
3. Add `Do`, `Then`, `Match`, `Let`, `Brackets` one at a time; run the corpus after each.
4. Run the full gate + `dd`. Decide.
