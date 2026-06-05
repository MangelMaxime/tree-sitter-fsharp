; F# local-scope queries for Helix. Helix resolves each @local.reference to an
; in-scope @local.definition and recolours it with the definition's class (the
; suffix after `@local.definition.`). See examples/locals.fsx for test cases.
; https://docs.helix-editor.com/master/guides/locals.html

; ── Scopes ───────────────────────────────────────────────────────────────────
; tree-sitter has an implicit root scope, so these aren't needed to *resolve* a
; binding's own uses — they ISOLATE bindings so same-named ones in sibling
; scopes don't leak together and cross-resolve.
(let_binding) @local.scope
(lambda_expression) @local.scope
(member_defn) @local.scope
(for_expression) @local.scope
(match_arm) @local.scope

; ── Definitions ──────────────────────────────────────────────────────────────
; Parameters (covers `x` and the `(x: int)` typed form).
(parameter (identifier) @local.definition.variable.parameter)

; Nested let-names; `function` matches how highlights.scm colours let-names.
(let_decl_indented name: (identifier) @local.definition.function)

; `use r = …` resource bindings; `variable` matches highlights.scm.
(use_binding name: (identifier) @local.definition.variable)

; For-loop variable (the loop's single direct identifier).
(for_expression (identifier) @local.definition.variable)

; Match / pattern bindings; the lowercase guard leaves uppercase constructor
; heads to highlights' @constructor rule.
((identifier_pattern (long_identifier (identifier) @local.definition.variable))
 (#match? @local.definition.variable "^[a-z_]"))

; ── References ────────────────────────────────────────────────────────────────
; Root segment of an identifier path (so `x` in `x.Length` is a reference, but
; `.Length` is not).
(long_identifier . (identifier) @local.reference)
