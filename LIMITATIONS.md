# Known limitations

Structural gaps that diverge from "ideal" parsing behavior. Each one is
tracked deliberately — listed here so future-me knows the trade-off, the
workaround if any, and what would have to change to fix it properly.

---

## Multi-statement `for` body outside a query CE parses as a chained application

**What:** `for_expression`'s body uses `optional($._expression)` instead
of the symmetric `_indented_or_inline_body` shape that `if`/`while`/
`lambda` use. So:

```fsharp
for i in [1; 2; 3] do
    printfn "item %d" i
    printfn "double: %d" (i * 2)
```

The two `printfn` calls are parsed as ONE `application_expression`
chain (the second `printfn` becomes an "argument" of the first), not a
`sequence_expression` with two siblings.

**User-visible effects:**
- Expand-selection from `"double: %d"` walks through invented
  application wrappers instead of landing on the statement.
- Textobjects still work (the whole body is captured under
  `body:`); only the internal structure is wrong.

**Workaround:** none. `while` and `lambda` got the fix; `for` didn't.

**Why we accepted it:** switching `for_expression` to
`_indented_or_inline_body` breaks query computation expressions:
`query { for x in xs do where … select … }` parses `where (…)` as the
for-body because the `query_ce` reserved keyword set doesn't propagate
through `_body_indent`. The blast radius is documented in `grammar.js`.

**Fixable by:** introducing a private `_ce_for_clause` rule (no body)
aliased to `for_expression`, used only inside `_ce_statement`. Then
`for_expression` can switch to `_indented_or_inline_body`. ~30 grammar
lines, low risk. See the "Cleanup #1" thread in the session notes.

---

## Pattern for adding new entries

Each entry follows the same shape:

1. **What** — concrete code example, parse-tree symptom.
2. **User-visible effects** — what breaks in Helix / textobjects /
   expand-selection.
3. **Workaround in place** — current mitigation, if any, with file path.
4. **Why we accepted it** — the cost of the proper fix.
5. **Fixable by** — what the proper fix would look like.
