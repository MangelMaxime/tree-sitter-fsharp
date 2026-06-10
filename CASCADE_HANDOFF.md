# Handoff: tree-sitter F# accumulated-offside-state cascades (NetUtils / NuGetV3)

You are picking up the hardest remaining class of bugs in an existing **tree-sitter F# grammar**
used by Helix for syntax highlighting. A prior model (Opus 4.8) investigated, ruled out the
obvious hypotheses, and is handing you a *verified* problem statement. Read this whole doc first.

> **Important correction up front.** This was initially mis-described as a "preprocessor
> (`#if/#else`) redesign." That was WRONG and has been disproved (see §3). The real problem is
> the **offside/layout scanner desyncing and cascading on certain whole files**, even though
> every isolated slice of those files parses cleanly. There is already a design sketch for the
> intended fix in `LAYOUT_REWRITE.md` at the repo root — read it; this handoff is the empirical
> companion to it.

---

## 0. Repo, constraints, workflow

- Repo root: `/home/mmangel/Workspaces/Github/MangelMaxime/tree-sitter-fsharp-helix/main`, branch `main`.
- Grammar isn't public → you may update `test/corpus/*.txt` for intentional tree-shape changes,
  but node names used by `queries/*.scm` must stay stable.
- **Editable:** `grammar.js`, `src/scanner.c`. The rest of `src/*` (`parser.c`, `grammar.json`,
  `node-types.json`) is GENERATED — regenerate with `tree-sitter generate grammar.js`, never hand-edit.
- After changes: `./dev.sh` deploys parser + queries to Helix.
- **Commits:** the human commits. Never commit without explicit per-commit consent; propose a
  plain `type: subject` message and wait.
- `/tmp/bench/**` = READ-ONLY benchmark clones. Anonymize anything you copy into tests/examples.
- Design context already in the repo: `LAYOUT_REWRITE.md` (the uniform-layout scanner rewrite
  plan). The current `src/scanner.c` IS an offside scanner (a partial version of that model).

## 1. The problem in one sentence

For a handful of large files, the external offside scanner's accumulated indent/layout state
drifts partway through the file; from the drift point onward the parse desyncs and emits ERROR
nodes all the way to EOF — yet **every small slice of the same file parses with 0 errors**. The
bug lives in `src/scanner.c`'s layout state machine (column stack / virtual-semi / body
open-close logic), not in any single grammar rule.

## 2. The two concrete cases (current `main`)

| File | ERROR nodes | Notes |
|---|---|---|
| `/tmp/bench/Paket/src/Paket.Core/Common/NetUtils.fs` | 50 | 665 lines; desync begins ~line 80–92 |
| `/tmp/bench/Paket/src/Paket.Core/Dependencies/NuGetV3.fs` | 75 | 825 lines; partly trailing-`;` (see §4) + accumulated state |

Symptom signature (verified): the whole file is wrapped in one `(ERROR [0,3] - [EOF])` node, and
`head -N | parse` shows error count rising **monotonically** as N grows past the drift point
(e.g. NetUtils no-preproc: head-100→3, 150→5, 200→6, 250→10, 300→18, 400→21, 500→29). That
monotonic growth is the fingerprint of accumulated-state desync (vs. a localized bug, which
would add a fixed small count regardless of N).

## 3. What was RULED OUT (don't repeat these)

- **Preprocessor is NOT the cause of NetUtils.** Proof: blanking every `#if/#elif/#else/#endif`
  directive line (keeping both branches' code) leaves NetUtils at **50 errors, unchanged**:
  ```bash
  sed -E 's/^[[:space:]]*#(if|elif|else|endif).*$//' \
    /tmp/bench/Paket/src/Paket.Core/Common/NetUtils.fs > /tmp/np.fsx
  tree-sitter parse /tmp/np.fsx 2>/dev/null | grep -c ERROR   # => 50
  ```
  (Preproc is modelled as `extras` in `grammar.js` — `$.preproc_if` etc. around line 190; this
  works fine for the common case and is a red herring here.)
- **Not slice-isolable.** The exact region around the desync parses 0 errors when extracted:
  ```bash
  sed -n '84,113p' /tmp/bench/Paket/src/Paket.Core/Common/NetUtils.fs | tree-sitter parse /dev/stdin 2>/dev/null | grep -cE 'ERROR|MISSING'  # => 0
  ```
  Candidate idioms in that region (all parse fine in isolation, so none is "the" root alone):
  `if cond then X else` with the `else` body dedented to the binding column (lines 81–82, 85,
  92); nested `let` helpers; `match … with` arms after such an `if`. The drift is the
  *combination* + accumulated indent stack, not any one of them.

## 4. NuGetV3 has a partially-fixed sub-issue (context)

NuGetV3's cascade is compounded by a **trailing `;` after a body** (`let getDir () = … directory;`).
A grammar change already landed on `main` that fixes the SINGLE-statement form
(`_ascribable_body` now `seq(choice(_expression, type_ascription_expression), optional(";"))`
— see `grammar.js`), which cleared e.g. FAKE/SemVerHelper.fs with 0 regressions. But NuGetV3's
actual case is a **multi-statement layout-block body** whose last statement carries the `;`:
```fsharp
let private getCatalogPageDirectory(basePath, item) =
    let hostName = …
    if directory.Exists |> not then
        directory.Create()
    directory;            // <-- trailing ; on the last stmt of a layout block
```
Here the `;` sits *before* the scanner's `_layout_end` (the body closes by dedent on the next
line), and grammar-level `optional(";")` on `sequence_expression` did NOT consume it (tried,
no effect, reverted). This is a SCANNER-level interaction: at a layout close, a trailing `;`
needs to be accepted/consumed. Fixing it is part of the same layout-state work. Minimal repro:
```bash
printf 'let f () =\n    g ()\n    a;\n' | tree-sitter parse /dev/stdin 2>/dev/null | grep -cE 'ERROR|MISSING'  # => 2 (should be 0)
printf 'let f () =\n    a;\n'           | tree-sitter parse /dev/stdin 2>/dev/null | grep -cE 'ERROR|MISSING'  # => 0 (single-stmt already fixed)
```

## 5. The scanner you're working in

`src/scanner.c` is an offside-rule external scanner. Key externals (declared in `grammar.js`
`externals:` ~line 153, implemented in the scanner): `_layout_open/_layout_semi/_layout_end`
(generic body open / sibling-separator / close), `_match_open/_match_end`, `_bracket_open/
_bracket_semi/_bracket_close`, `_record_open`, `_block_open` (module bodies), `_type_open`,
`_expr_open` (then/elif/else/lambda/let-in value), `_else_open`, `_for_open`, `_try_open`,
plus zero-width attribute/element gates. The scanner keeps a stack of layout contexts with
captured columns and decides open/close/separate by comparing the next line's indent to the
context column. The cascades are failures in that column tracking / close-timing under
accumulated state.

`LAYOUT_REWRITE.md` describes the intended uniform model (Haskell-style: grammar-driven opens,
`valid(END)`-gated closes, multi-level dedent). The realistic paths are (a) targeted hardening
of the current scanner around the drift idioms, or (b) executing the rewrite in
`LAYOUT_REWRITE.md`. Opus 4.8's read: targeted patches keep hitting "fixed one file, drifted
another" because the state model is approximate; the rewrite is the durable fix but is a large,
high-risk change. Confirm empirically; don't assume.

## 6. Measurement methodology (has sharp edges — read carefully)

Benchmark: 10 OSS repos under `/tmp/bench/`, file list `/tmp/bench/filelist.txt` (1544 files).

**Rebuild cleanly before every measurement** (stale cache lies):
```bash
cd <repo>; rm -rf ~/.cache/tree-sitter; tree-sitter generate grammar.js >/dev/null
printf 'let x = 1\n' | tree-sitter parse /dev/stdin >/dev/null   # warm (compiles scanner)
```
**Batch sweep** (prints one line per file containing ERROR):
```bash
xargs -a /tmp/bench/filelist.txt tree-sitter parse --quiet 2>/dev/null \
  | grep -oP '^\S+(?=.*ERROR)' | sort -u > /tmp/after.txt
```
Baseline diff vs committed HEAD: build HEAD the same way into `/tmp/before.txt`, then
`comm -13 before after` = regressions, `comm -23 before after` = fixed.

- **GOTCHA: batch vs single-parse disagree on big cascade files.** The batch shares process
  state and miscounts already-broken files. **Single-parse `tree-sitter parse <file>` is
  authoritative per file** — confirm every batch-flagged file with single-parse.
- **GOTCHA: git worktrees share `~/.cache/tree-sitter`.** Stay in ONE dir; swap versions in
  place (`git show HEAD:grammar.js > grammar.js` / `git stash`) and always rm-cache + regen +
  warm between versions.
- Always count `ERROR|MISSING` (a MISSING can hide behind a clean ERROR grep).
- Gates: `tree-sitter test` must stay green (currently 404/404); `examples/layout.fsx` must
  stay 0 errors (it's the curated offside-construct harness — add your repro there).

## 7. Success criteria

- NetUtils.fs and NuGetV3.fs error counts drop materially toward 0 (single-parse verified).
- Batch sweep shows **0 new error files** vs committed HEAD (confirm each flagged file by
  single-parse — beware the batch gotcha).
- `tree-sitter test` green; `examples/layout.fsx` 0 errors; add corpus tests + a layout.fsx
  example for any idiom you fix.
- A net-negative or flat result is an acceptable, honest outcome to report — the prior model's
  experience is that targeted layout patches often trade one file for another. If you can't get
  a clean net win, say so with numbers rather than shipping churn.

## 8. Fast start

```bash
cd /home/mmangel/Workspaces/Github/MangelMaxime/tree-sitter-fsharp-helix/main
rm -rf ~/.cache/tree-sitter; tree-sitter generate grammar.js >/dev/null
printf 'let x = 1\n' | tree-sitter parse /dev/stdin >/dev/null
tree-sitter parse /tmp/bench/Paket/src/Paket.Core/Common/NetUtils.fs 2>/dev/null | grep -c ERROR   # 50
# slices are clean — prove it to yourself, then study accumulated state:
sed -n '84,113p' /tmp/bench/Paket/src/Paket.Core/Common/NetUtils.fs | tree-sitter parse /dev/stdin 2>/dev/null | grep -cE 'ERROR|MISSING'  # 0
for N in 70 90 150 300 500; do echo -n "head -$N: "; head -n $N /tmp/bench/Paket/src/Paket.Core/Common/NetUtils.fs | tree-sitter parse /dev/stdin 2>/dev/null | grep -cE 'ERROR|MISSING'; done
# read the design:
cat LAYOUT_REWRITE.md
```

Good luck. The honest prior assessment: this is a layout-scanner-state problem of moderate-to-
high difficulty; targeted hardening is ~40-50% likely to net-improve without regressions, the
full `LAYOUT_REWRITE.md` rewrite is the durable fix but large and risky. It is NOT a preproc
problem (that was disproved).
