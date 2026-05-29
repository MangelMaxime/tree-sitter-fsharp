; Rainbow brackets for F#. Deliberately minimal:
;   • `( … )` parentheses (everywhere they appear)
;   • `[ … ]` only on `list_expression`

[
  ; ( … )
  (parenthesized_expression)
  (parenthesized_type)
  (operator_name)              ; `(+)`, `(>>=)`, `(!//!)`
  (tuple_pattern)              ; `( a, b )`
  (tuple_params)               ; `( a: int, b: int )` in secondary ctor / member
  (struct_tuple_expression)    ; `struct (1, 2)`
  (struct_tuple_pattern)       ; `match _ with struct (a, b) -> …`
  (primary_constructor)        ; `type C(x: int, y: int)` — the non-unit form

  ; [ … ]  on list expressions only
  (list_expression)
] @rainbow.scope

[
  "(" ")"
] @rainbow.bracket

; `[` and `]` only when they bracket a list_expression — leaves
; `list_pattern`, `array_*`, `attribute`, `index_expression`, etc. alone.
(list_expression ["[" "]"] @rainbow.bracket)
