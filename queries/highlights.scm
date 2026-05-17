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
(xml_doc_comment) @comment.documentation
(block_comment) @comment.block
(block_doc_comment) @comment.block.documentation

(int_literal) @constant.numeric.integer
(float_literal) @constant.numeric.float
(bool_literal) @constant.builtin.boolean
(unit) @constant.builtin
(null_literal) @constant.builtin
