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
  "when"
  "as"
  "in"
  "inline"
  "mutable"
] @keyword

"not" @keyword.operator

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
  "+"
  "-"
  "*"
  "/"
  "%"
  "&&"
  "||"
  "|>"
  "<|"
  ">>"
  "<<"
  "::"
  "~~~"
] @operator

(symbolic_op) @operator

":" @punctuation.delimiter

[
  "("
  ")"
  "["
  "]"
  "[|"
  "|]"
] @punctuation.bracket

[
  ";"
  ","
] @punctuation.delimiter

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

; Named types anywhere in a type expression
(type_expression (long_identifier) @type)
(generic_type (long_identifier) @type)
(postfix_type (long_identifier) @type)
(type_parameter) @type.parameter

(import_decl
  [
    (long_identifier) @namespace
  ])
