; F# indent queries for Helix.
;
; Helix's algorithm in brief:
;   indent(line) = (sum of @indent ancestors enclosing this position)
;                - (sum of @outdent tokens at the start of this line).
;
; So `@indent` goes on the *parent* whose contents should be deeper, and
; `@outdent` goes on tokens (or nodes) that should pull the current line
; back out — e.g. closing brackets, or keywords like `else` that sit at the
; parent's indent level.

; ── Bindings ──────────────────────────────────────────────────────────────────
; `let x = …` — body on subsequent lines is indented under the binding.
[
  (let_decl_indented)
  (use_binding)
  (secondary_constructor)
] @indent

; `let x =` (no body yet) and `and x =` (no body yet) — mid-edit shape.
; `!body` matches only when the body field is absent, so completed bindings
; like `let x = 1` (body field present) don't trigger this @indent @extend.
((let_binding !body) @indent @extend)
((let_and_binding !body) @indent @extend)

; ── Functions, lambdas, members ───────────────────────────────────────────────
; member_defn is now a CHILD of type_decl, so when a member has its body the
; indent for "Enter after the body" comes from the enclosing type_decl
; (sibling-member column). The mid-edit case (`member this.Foo() =` with no
; body yet, body field absent) still needs @indent @extend so the next line
; lands at the body indent.
((member_defn !body) @indent @extend)
((abstract_member_defn) @indent @extend)
((property_accessor !body) @indent @extend)

[
  (lambda_expression)
  (function_expression)
] @indent

; ── Control flow ──────────────────────────────────────────────────────────────
; if/elif/else share one `if_expression` node — @outdent on `else`/`elif`
; pulls those keyword lines back to the `if` column.
[
  (if_expression)
  (try_expression)
  (for_expression)
  (while_expression)
  (ce_match_bang_expr)
] @indent

; `match_expression` itself is *not* @indent: F# style is `|` aligned with
; `match` (Microsoft style guide). The body *of* each arm (after `->`) is
; still indented because `match_arm` carries @indent.
(match_arm) @indent

; ── Type-level declarations ───────────────────────────────────────────────────
; Only indent when the declaration actually opens a body — i.e. has a trailing
; `=` (or, for type_extension, the `with` keyword). `type kg` and `type Foo`
; without a body must NOT indent.
;
; The inline-child syntax `(node "=")` matches only nodes that contain the
; literal `=` as a child, so these patterns gate the @indent / @extend on
; the presence of the `=` token.
((type_decl "=") @indent @extend)
((type_and_decl "=") @indent @extend)
(type_extension) @indent @extend

; Explicit block forms (`= class … end`, etc.) carry members as children and
; need their own @indent so members are indented under the keyword.
[
  (record_type_defn)
  (anonymous_record_type)
  (struct_type_defn)
  (class_type_defn)
  (interface_type_defn)
] @indent

; ── Modules / namespaces ──────────────────────────────────────────────────────
; `module Foo =` (with `=`) opens a nested module body — indent.
; `module Foo.Bar` and `namespace Foo` are file-scoped — siblings stay at col 0,
; so we don't capture them.
((module_decl "=") @indent @extend)

; ── Expressions with delimited bodies ─────────────────────────────────────────
; Anything with `{ … }`, `[ … ]`, `[| … |]`, `{| … |}`, `( … )`, or `begin … end`
; produces a multi-line body when the contents wrap. @outdent on the closing
; token below brings the closer back to the opener's column.
[
  (record_expression)
  (anonymous_record_expression)
  (list_expression)
  (array_expression)
  (object_expression)
  (computation_expression)
  (parenthesized_expression)
  (parenthesized_type)
  (begin_end_expression)
  (tuple_params)
  (tuple_pattern)
  (tuple_expression)
] @indent

; ── Outdent: keywords that sit at the parent indent ───────────────────────────
; `else` and `elif` are inside `if_expression` (which is @indent), so without
; @outdent the keyword lines would be one level too deep.
[
  "else"
  "elif"
] @outdent

; ── Outdent: closing tokens ───────────────────────────────────────────────────
[
  ")"
  "]"
  "}"
  "|]"
  "|}"
  "end"
] @outdent
