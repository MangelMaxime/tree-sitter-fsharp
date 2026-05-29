; Symbol tags for F#. Used by Helix's symbol picker (`<space>s`,
; `<space>S`) and workspace symbol search.
;
; Capture conventions:
;   @name           — the identifier the symbol is jumped to.
;   @definition.X   — marks the whole definition node and its category.

; ── Module / namespace ──────────────────────────────────────────────

(namespace_decl
  name: (long_identifier) @name) @definition.module

(module_decl
  name: (long_identifier) @name) @definition.module

; ── Type declarations ───────────────────────────────────────────────
; All `type Foo = …` and `and Foo = …` forms — records, DUs, classes,
; aliases, struct/enum types. One bucket keeps navigation predictable.

(type_decl
  name: (identifier) @name) @definition.type

(type_and_decl
  name: (identifier) @name) @definition.type

(exception_decl
  name: (identifier) @name) @definition.type

; ── Let bindings ────────────────────────────────────────────────────
; Functions: let with at least one parameter, operator-name bindings,
; and active patterns.

(let_binding
  name: (identifier) @name
  parameters: (parameter)) @definition.function

(let_binding
  name: (operator_name) @name
  parameters: (parameter)) @definition.function

(let_binding
  name: (active_pattern_name) @name) @definition.function

; Value bindings (`let pi = 3.14`) — `!parameters` matches only when
; the parameters field is absent, so this fires for paramless lets
; only and doesn't double-tag with @definition.function above.
(let_binding
  name: (identifier) @name
  !parameters) @definition.constant

; let-and chains: `let rec foo … and bar …`. The `and bar` half is
; `let_and_binding`; same function/constant split as above.
(let_and_binding
  name: (identifier) @name
  parameters: (parameter)) @definition.function

(let_and_binding
  name: (identifier) @name
  !parameters) @definition.constant

; ── Members ─────────────────────────────────────────────────────────

(member_defn
  name: (identifier) @name) @definition.function

(member_defn
  name: (operator_name) @name) @definition.function

(abstract_member_defn
  name: (identifier) @name) @definition.function

; `new (args) = …` — secondary class constructor.
(secondary_constructor "new" @name) @definition.function

; ── Fields ──────────────────────────────────────────────────────────

(record_type_field
  name: (identifier) @name) @definition.field

(val_field
  name: (identifier) @name) @definition.field

(union_case_field
  name: (identifier) @name) @definition.field

; ── Enum / DU cases ─────────────────────────────────────────────────

(union_case
  name: (identifier) @name) @definition.enum

(enum_case
  name: (identifier) @name) @definition.enum
