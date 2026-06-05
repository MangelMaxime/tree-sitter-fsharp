; F# local-scope queries for Helix.
;
; Per https://docs.helix-editor.com/master/guides/locals.html :
;   @local.scope               — a scope boundary; a definition is visible to
;                                references in the same scope or nested ones.
;   @local.definition.<class>  — a binding site. The <class> SUFFIX is the
;                                highlight Helix applies to any reference that
;                                resolves to this definition (so it must match
;                                the colour highlights.scm gives the binding).
;   @local.reference           — a use site; Helix resolves it to a definition
;                                and recolours it with the definition's class.
;
; Definitions keep their highlights.scm colour; references that DON'T resolve
; (e.g. `System`, `List`) keep theirs too — so this only adds colour to
; otherwise-uncoloured local use-sites and never needs a highlights.scm change.

; ── Scopes ──────────────────────────────────────────────────────────────────
(source_file) @local.scope

[
  (let_binding)
  (let_decl_indented)
  (let_and_binding)
  (lambda_expression)
  (for_expression)
  (member_defn)
] @local.scope

; ── Definitions ───────────────────────────────────────────────────────────────
; Parameters → references read as `variable.parameter` (matches highlights.scm).
(parameter (identifier) @local.definition.variable.parameter)
(tuple_param (identifier) @local.definition.variable.parameter)

; `let`-bound names → `function` (highlights.scm colours every let-name @function),
; captured inside their own scope so recursive self-references resolve.
(let_binding name: (identifier) @local.definition.function)
(let_decl_indented name: (identifier) @local.definition.function)
(let_and_binding name: (identifier) @local.definition.function)

; `for x in xs do …` loop variable → references read as `variable`.
(for_expression . (identifier) @local.definition.variable)

; ── References ────────────────────────────────────────────────────────────────
; Root segment of an identifier path; the `.` anchor keeps it to the first
; child so `x` in `x.Length` is a reference but `.Length` (a member) is not.
(long_identifier . (identifier) @local.reference)
