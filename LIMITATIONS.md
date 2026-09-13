# Limitations

Known trade-offs, with the evidence behind each. Referenced from `grammar.js`.

This grammar is built for syntax highlighting in Helix and Zed. Where correctness for a
type checker and usefulness for an editor pull apart, the editor wins. That is a choice,
not an oversight, and the entries below say which is which.

## Measuring quality

`task score` reports four axes; a single clean-parse percentage is not a quality
measure for a grammar this permissive.

| axis | what it means |
|---|---|
| coverage | valid F# parses without error sites |
| rejection | invalid F# is flagged - scored as recall on *syntax* diagnostics and false positives on *type*-only ones, separately |
| degeneracy | reserved keywords lexed as `identifier` in files with zero error sites - wrong trees no error count can see |
| highlight | fraction of `queries/highlights.scm` captures that a `test/highlight` assertion pins |

## Accepted: arbitrary text can parse without error nodes

Verified against the current parser, all with zero ERROR/MISSING nodes:

```
The quick brown fox jumps over the lazy dog
this is complete garbage text
public class Foo { public int Bar { get; set; } }
```

`word: $ => $.identifier` means a keyword whose token is not valid in the current LR state
lexes as an `identifier`, and `application_expression` accepts any expression followed by
any simple expression. Juxtaposed identifiers are therefore always a valid parse.

Nothing turns unparsed text into a *comment* - every comment producer is delimiter-anchored
and yields a real ERROR when unterminated.

`reserved.global` is being populated to narrow this, but only where it costs no valid code
(see below).

## Accepted: reserved-word coverage stops short of the full keyword list

`reserved.global` holds the keywords that are never a bare identifier *and* cost nothing.
The remainder were each measured and rejected:

| keyword | why not |
|---|---|
| `lazy` | `e: lazy<int>` is a type name |
| `extern` | heads `extern_decl`'s C-style form |
| `begin` | verbose syntax `do a then begin b end` |
| `fun` `open` `override` `while` | each cost a valid bench file, inside a `#if` branch where the keyword degrades to an identifier today |
| `class` `end` | retried after `_decl_semi` removed their original blocker; still cost 13 valid dotnet/fsharp files for zero recall or degeneracy gain |
| `of` `private` | zero measured gain, and cost 7 valid files |
| `member` | correct in principle, but a type whose body is on the `=` line (`type DU = | A`) has no slot for members below it, so 13 files would turn from wrongly-parsed into error regions |
| `base` `global` `fixed` `void` `not` | legal identifiers in real F# |
| query operators (`where`, `select`, …) | legal identifiers; handled contextually by the `query_ce` reserved set |

`if`, `then`, `elif` and `else` are reserved since 2026-09-12. Reserving them turned the
remaining wrong trees around `if` into visible errors, which is how the dangling-`else`,
`else`-at-end-of-line and constructor-`then` layouts were found and fixed.

`type` is reserved and now costs nothing. It initially broke one file - a statement, then a
`;`-terminated statement, then a declaration - which the `_decl_semi` external token fixed:
the scanner peeks past the `;` and, when a declaration keyword follows, emits it as an
extra so the `;` never reaches `sequence_expression`. LR(1) cannot make that call, because
it shifts on the `;` alone and only fails a token later.

The safe keywords are safe precisely because nobody misuses them, so reserving them changes
little. The rejection gains live in the risky tail.

## Accepted: an unterminated `"` consumes following lines

`_string_content` matches newlines because F# ordinary string literals legally contain
them:

```fsharp
let s = "line one
line two"
```

Forbidding `\n` would break valid code. The runaway-string behaviour is inherent to the
language, not a grammar defect.

## Accepted: `;;` is skippable anywhere

`fsi_terminator` sits in `extras`, so `[1;;2]` parses as one element rather than being
rejected (it is invalid F#, FS0010). 217 corpus files rely on `;;` as a statement
terminator against 3 occurrences of the bad shape, two of which are test fixtures.
Removing it from `extras` is a bad trade.

## Accepted: `#if` conditions absorb the rest of the line

`preproc_expression` is `token(/[^\n\r]+/)` and `preproc_if` is an `extra`, so any
`#if <anything>` line is skippable in any state. Tightening it must still accept the ~49 of
430 distinct corpus conditions that carry a trailing `//` or `(* *)` comment.

## Resolved: a non-CE `for` with a multi-statement body

```fsharp
for x in xs do
    printfn "a"
    printfn "b"
```

The body is a layout opened by the `_for_open` scanner token, so the statements sequence.
The token is withheld when the next line sits at the enclosing CE column, which keeps a
query `for x in xs do` followed by `where`/`select` body-less.

## Known gap: dotted chains of 3+ segments

1-2 segments stay one `long_identifier`; 3+ nest as `dot_expression(long_identifier(a,b),c)`.
Irregular, and not fixable with tree-sitter's LR generator. Every highlight rule and
textobject handles both shapes.

## Resolved: members below a same-line type body

```fsharp
type DU = | A
          member this.F = 1
```

Handled by the `_members_open` scanner token: emitted only when the line after a same-line
body indents past the enclosing context and starts with a member keyword or `[<`. An
ungated `_type_open` in that slot corrupted the layout stack (644 failing files); the
keyword gate is what makes it safe.

## Known gap: keywords lexed as identifiers

`task score` reports 20 such sites across the bench corpus (255 on 2026-09-12 before the
layout work). What is left:

| shape | sites | status |
|---|---|---|
| `#if` / `#else` branches that share one declaration head or an unbalanced `(` | 8 | accepted: both branches parse as code |
| `as` alias on an element of a tuple parameter (`(a: T, b, x as data)`) | 3 | costs 243 parser states |
| `use x = e in body` on one line | 1 | costs 227 parser states |

## Accepted: constructs dropped for parser size

`tree-sitter generate` time and the compiled parser size scale with the dense parse
table, `LARGE_STATE_COUNT x SYMBOL_COUNT` in `src/parser.c` (15,515 states, 566
symbols and a 9.1 MB `.so` at the time of writing; 22,364 states and 14.0 MB before the
sharing described under *Keeping the parser small*). These forms parsed at one point
but cost more states than their bench impact justified, and were removed on
2026-09-12:

| construct | states | bench sites |
|---|---|---|
| bare ascription inside `<@ … @>` and after a comprehension `->` | 1,304 | 4 |
| bodiless `member X: T` / `static member X: T` (signature files) | 731 | .fsi only |
| `val x<'T>: T` type parameters and `val x: T when …` | 624 | .fsi only |
| `val x = expr` initialiser (`[<Literal>] val X: int = 3` still parses) | 512 | 2 |
| access modifier or attribute on a `get`/`set` accessor | 360 | 30 |
| `inherit B() with` followed by members | 342 | 2 |
| `{ new R with a = 1 and b = 2 }` legacy object members | 108 | 1 file |
| `T \| null` as a type abbreviation, inside parens, on `#T`, in `(# … #)` | 255 | 15 |
| `Generic<'T>.Nested`, `'T & #I`, `< >` | 152 | 9 |
| `use! (_) = …`, `use! (a, b) = …` name patterns (`use (x: T)` and `use x : T` parse) | 145 | 12 |
| `as` alias on a tuple-parameter element, `use x = e in body` on one line | 243, 227 | 3, 1 |

Signature files (`.fsi`) are therefore only partially supported and need their own
grammar (a signature grammar inheriting this one, as Ionide does) rather than more
alternatives in the shared rules.

## Keeping the parser small

Parser states are LR item sets. A fragment inlined at several sites, or a rule whose
optional head elements precede a large tail, generates a separate copy of every state the
fragment reaches. Two edits keep the table small:

- Give an identical fragment one hidden rule (`_layout_body`, `_paren_args`,
  `_srtp_member_sig`, ...) instead of repeating it.
- Split a declaration rule at its `=` into the head and a hidden `_x_rhs` rule, and put the
  parent's `prec.right` / `prec.dynamic` on the new rule.
- Gate a rule that shares a prefix with a bigger cluster behind a zero-width scanner token
  (`_label_gate` for `name: T` inside type expressions). The parser then never forks on
  the shared prefix, so the cluster is not cloned per fork.
- Open layout blocks from the scanner instead of adding grammar alternatives: `(`, a
  record field `=`, and a line-ending `&&`/`||` each push a body context
  (`_paren_block_open`, `_field_block_open`, `_infix_block_open`) that the existing
  `_layout_semi` / `_layout_end` tokens close. One token each, no new expression forks.

A family of keywords used in one position may be one `token(prec(1, choice(...)))` aliased
to a named node (`query_op`); drop them from the `reserved` list, since reserved words must
be tokens. Measure every grammar change with the `STATE_COUNT` and `SYMBOL_COUNT` defines
before and after.
