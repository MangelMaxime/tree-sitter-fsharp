; Neovim fold regions (`@fold`, nvim-treesitter dialect): every node whose
; body usually spans lines. Helix folds by indentation and Zed reads
; queries/zed/, so this file is Neovim-only.

[
  (module_decl)
  (let_binding)
  (let_decl_indented)
  (let_and_binding)
  (type_decl)
  (type_and_decl)
  (type_extension)
  (exception_decl)
  (member_defn)
  (secondary_constructor)
  (property_accessor)
  (interface_impl)
  (match_expression)
  (function_expression)
  (match_arm)
  (if_expression)
  (for_expression)
  (while_expression)
  (try_expression)
  (lambda_expression)
  (computation_expression)
  (record_expression)
  (anonymous_record_expression)
  (list_expression)
  (array_expression)
  (object_expression)
  (parenthesized_expression)
  (begin_end_expression)
  (typed_quotation)
  (untyped_quotation)
  (block_comment)
  (block_doc_comment)
] @fold

; Consecutive line comments and `///` doc blocks fold as one region.
(line_comment)+ @fold
(xml_doc_comment)+ @fold
