[
  "open"
  "type"
] @keyword

(import_decl
  [
    (long_identifier) @namespace
  ])

"." @punctuation

(line_comment) @comment.line

(int_literal) @constant.numeric.integer
(float_literal) @constant.numeric.float
(bool_literal) @constant.builtin.boolean
(unit) @constant.builtin
(null_literal) @constant.builtin
