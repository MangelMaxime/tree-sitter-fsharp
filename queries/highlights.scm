[
  "namespace"
  "module"
  "open"
  "type"
  "let"
  "rec"
  "fun"
  "as"
  "in"
  "inline"
  "mutable"
  "of"
  "member"
  "override"
  "default"
  "abstract"
  "inherit"
  "interface"
  "static"
  "val"
  "do"
  "use"
  "new"
  "exception"
  "let!" "do!" "use!"
] @keyword

[
  "private"
  "internal"
  "public"
] @keyword.control.access

["not" "upcast" "downcast"] @keyword.operator

[
  "|"
  "->"
  "if" "then" "else" "elif"
  "match" "with" "when"
  "try" "finally"
  "for" "while"
  "return" "return!"
  "yield" "yield!"
  "match!"
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
  "<-"
  ".."
  ":>"
  ":?>"
  ":?"
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
  "{"
  "}"
  "[<"
  ">]"
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

(let_binding
  name: (operator_name) @function)

(lambda_expression
  (parameter
    (identifier) @variable.parameter)*)

; Named types anywhere in a type expression
(type_expression (long_identifier) @type)
(generic_type (long_identifier) @type)
(postfix_type (long_identifier) @type)
(type_parameter) @type.parameter

(attribute_target
  name: (long_identifier) @attribute)

(namespace_decl
  name: (long_identifier) @namespace)

(module_decl
  name: (long_identifier) @namespace)

(import_decl
  [
    (long_identifier) @namespace
  ])

(member_self_ident) @variable.builtin

(member_defn
  name: (identifier) @function)

(abstract_member_defn
  name: (identifier) @function)

(val_field
  name: (identifier) @variable.other.member)

(type_decl
  name: (identifier) @type)

(exception_decl
  name: (identifier) @type)

(type_decl
  alias: (type_expression) @type)

(union_case
  name: (identifier) @constructor)

(record_type_field
  name: (identifier) @variable.other.member)

(record_field
  name: (long_identifier) @variable.other.member)

; Type name in new expressions (not wrapped in type_expression so needs its own capture)
(new_expression (long_identifier) @type)
(new_expression (generic_type (long_identifier) @type))

; Computation expression builder name (async, task, seq, promise, …)
(computation_expression
  builder: (long_identifier) @keyword)

; Exception-raising functions — highlighted like throw/raise in other languages
((long_identifier) @function.builtin
 (#match? @function.builtin "^(raise|reraise|failwith|failwithf|invalidArg|invalidOp|nullArg)$"))

; CE bang-binding names
(ce_let_bang_expr name: (identifier) @variable)
(ce_use_bang_expr name: (identifier) @variable)
(use_binding name: (identifier) @variable)
