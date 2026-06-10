; F# local-scope queries for Helix. Helix resolves each @local.reference to an
; in-scope @local.definition and recolours it with the definition's class (the
; suffix after `@local.definition.`). See examples/locals.fsx for test cases.
; https://docs.helix-editor.com/master/guides/locals.html

; ── Scopes ───────────────────────────────────────────────────────────────────
; tree-sitter has an implicit root scope, so these aren't needed to *resolve* a
; binding's own uses — they ISOLATE bindings so same-named ones in sibling
; scopes don't leak together and cross-resolve.
;
; NOTE: top-level / module / CE `let` NAMES are deliberately NOT resolved. They
; are `let_binding` names, and `let_binding` must stay a scope to isolate each
; function's parameters; that traps the name in its own scope. Making them
; resolve requires dropping the let_binding scope, which leaks every function's
; params to the file/module scope and breaks common param names (e.g. `x` with
; several definitions resolves to the wrong one). Tried 2026-06-05, reverted.
(let_binding) @local.scope
(lambda_expression) @local.scope
(member_defn) @local.scope
(for_expression) @local.scope
(match_arm) @local.scope
; `let x = … in cont` — the binding's use (cont) is INSIDE this node, so scoping
; it here is safe (no sibling-trap like top-level let_binding).
(let_expression) @local.scope

; ── Definitions ──────────────────────────────────────────────────────────────
; Parameters (covers `x` and the `(x: int)` typed form).
(parameter (identifier) @local.definition.variable.parameter)

; Nested let-names; `function` matches how highlights.scm colours let-names.
(let_decl_indented name: (identifier) @local.definition.function)

; `let x = … in …` (explicit-`in` form) — name is directly on let_expression.
(let_expression name: (identifier) @local.definition.function)

; `use r = …` resource bindings; `variable` matches highlights.scm.
(use_binding name: (identifier) @local.definition.variable)

; For-loop variable (the loop's single direct identifier).
(for_expression (identifier) @local.definition.variable)

; `as`-alias binding (`… as item`, `Some x as y`) — so its later uses resolve.
(as_pattern (identifier) @local.definition.variable)

; Deconstructed binding in a typed parameter `(Url url: Url)` — the
; long_identifier after the constructor; register so its uses resolve.
((destructure_parameter (long_identifier) (long_identifier (identifier) @local.definition.variable))
 (#match? @local.definition.variable "^[a-z_]"))

; Match / pattern bindings; the lowercase guard leaves uppercase constructor
; heads to highlights' @constructor rule.
((identifier_pattern (long_identifier (identifier) @local.definition.variable))
 (#match? @local.definition.variable "^[a-z_]"))

; Type-annotated binder name (`for s: string in`, `for k: T, r: T in`,
; `let (a: int, b)`) — the bound name sits directly in tuple_typed_pattern's
; `pattern`, so it isn't covered by identifier_pattern above.
((tuple_typed_pattern pattern: (long_identifier (identifier) @local.definition.variable))
 (#match? @local.definition.variable "^[a-z_]"))

; ── References ────────────────────────────────────────────────────────────────
; Root segment of an identifier path (so `x` in `x.Length` is a reference, but
; `.Length` is not).
(long_identifier . (identifier) @local.reference)
