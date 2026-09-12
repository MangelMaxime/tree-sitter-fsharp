; Neovim indent queries (nvim-treesitter dialect: @indent.begin / @indent.branch
; / @indent.auto). Unrelated to ../indents.scm (Helix's @indent/@outdent) and
; ../zed/indents.scm; the same constructs, spelled for Neovim's algorithm:
;   @indent.begin  lines inside this node are indented one level deeper
;   @indent.branch a line starting with this token is dedented to the
;                  node's own level (`else`, closing brackets)
;   @indent.auto   keep the previous line's indent inside these nodes

; ── Bindings, members, lambdas ────────────────────────────────────────────────
[
  (let_binding)
  (let_decl_indented)
  (let_and_binding)
  (use_binding)
  (member_defn)
  (abstract_member_defn)
  (property_accessor)
  (secondary_constructor)
  (lambda_expression)
  (function_expression)
] @indent.begin

; ── Control flow ──────────────────────────────────────────────────────────────
; `match` itself is not indented: F# style aligns `|` with `match`; the body
; of each arm (after `->`) is, via match_arm.
[
  (if_expression)
  (try_expression)
  (for_expression)
  (while_expression)
  (ce_match_bang_expr)
  (match_arm)
] @indent.begin

; ── Type-level declarations ───────────────────────────────────────────────────
; Only when the declaration opens a body (`type T =`), not `type kg`.
((type_decl "=") @indent.begin)
((type_and_decl "=") @indent.begin)
(type_extension) @indent.begin

[
  (record_type_defn)
  (anonymous_record_type)
  (struct_type_defn)
  (class_type_defn)
  (interface_type_defn)
] @indent.begin

; `module Foo =` opens a nested body; `module Foo.Bar` (file-scoped) does not.
((module_decl "=") @indent.begin)

; ── Delimited bodies ──────────────────────────────────────────────────────────
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
] @indent.begin

; ── Lines that start at the parent's level ────────────────────────────────────
[
  "else"
  "elif"
] @indent.branch

[
  ")"
  "]"
  "}"
  "|]"
  "|}"
  "end"
] @indent.branch

; ── Free-form text: never re-indent ──────────────────────────────────────────
[
  (string_literal)
  (verbatim_string)
  (triple_quoted_string)
  (multidollar_string)
  (line_comment)
  (xml_doc_comment)
  (block_comment)
  (block_doc_comment)
] @indent.auto
