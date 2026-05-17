[
  "open"
  "type"
  "let"
  "rec"
  "fun"
  "if"
  "then"
  "else"
  "elif"
  "match"
  "with"
] @keyword

[
  "|"
  "->"
] @keyword.control

"." @punctuation
[
  "="
  ">"
  "<"
  ">="
  "<="
  "<>"
] @operator

":" @punctuation.delimiter

[
  "("
  ")"
] @punctuation.bracket

(line_comment) @comment.line
(xml_doc_comment) @comment.line.documentation
(block_comment) @comment.block
(block_doc_comment) @comment.block.documentation

(int_literal) @constant.numeric.integer
(float_literal) @constant.numeric.float
(char_literal) @constant.character
(string_literal) @string
(verbatim_string) @string
(triple_quoted_string) @string
(bool_literal) @constant.builtin.boolean
(unit) @constant.builtin
(null_literal) @constant.builtin

(let_binding
  name: (identifier) @function
  parameters: (parameter
    (identifier) @variable.parameter)*)

(lambda_expression
  (parameter
    (identifier) @variable.parameter)*)

(type_expression
  [
    (identifier) @type
    (long_identifier) @type
  ])

(import_decl
  [
    (long_identifier) @namespace
  ])
