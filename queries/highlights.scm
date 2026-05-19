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
  "get" "set" "and"
  "lazy" "assert"
  "begin" "end"
  "function"
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
  "for" "while" "to" "downto"
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
  "{|"
  "|}"
  "[<"
  ">]"
] @punctuation.bracket

[
  ";"
  ","
] @punctuation.delimiter

(preproc_keyword) @keyword.directive

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
  name: (active_pattern_name) @function)

(let_binding
  name: (identifier) @function
  parameters: (parameter
    (identifier) @variable.parameter)*)

(let_binding
  name: (backtick_identifier) @function)

(let_binding
  name: (operator_name) @function)

(let_and_binding
  name: (identifier) @function
  parameters: (parameter
    (identifier) @variable.parameter)*)

(let_and_binding
  name: (backtick_identifier) @function)

(lambda_expression
  (parameter
    (identifier) @variable.parameter)*)

; Named types anywhere in a type expression
(type_expression (long_identifier) @type)
(generic_type (long_identifier) @type)
(postfix_type (long_identifier) @type)
(type_parameter) @type.parameter

; Type name in :? pattern  (| :? System.Exception as e ->)
(type_check_pattern (long_identifier) @type)
(type_check_pattern (generic_type (long_identifier) @type))
(type_check_pattern (postfix_type (long_identifier) @type))

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

(member_self_ident) @variable

(member_defn
  name: (identifier) @function)

(member_defn
  name: (backtick_identifier) @function)

(property_accessor (parameter (identifier) @variable.parameter))

(abstract_member_defn
  name: (identifier) @function)

(abstract_member_defn
  name: (backtick_identifier) @function)

(val_field
  name: (identifier) @variable.other.member)

(type_decl
  name: (identifier) @type)

(type_and_decl
  name: (identifier) @type)

(exception_decl
  name: (identifier) @type)

(type_decl
  alias: (type_expression) @type)

(union_case
  name: (identifier) @constructor)

(union_case_field
  name: (identifier) @variable.other.member)

(named_field_pat
  name: (identifier) @variable.other.member)

(union_case_field type: (long_identifier) @type)

(enum_case
  name: (identifier) @constructor)

(record_type_field
  name: (identifier) @variable.other.member)

(record_field
  name: (long_identifier) @variable.other.member)

(record_field_pattern
  name: (long_identifier) @variable.other.member)

; Member access on non-identifier expressions: arr.[0].Length, (f x).Name
(dot_expression member: (identifier) @variable.other.member)
(dot_expression member: (backtick_identifier) @variable.other.member)

; Type name in new expressions (not wrapped in type_expression so needs its own capture)
(new_expression (long_identifier) @type)
(new_expression (generic_type (long_identifier) @type))

; Computation expression builder name (async, task, seq, promise, …)
(computation_expression
  builder: (long_identifier) @keyword)

; Exception-raising functions — highlighted like throw/raise in other languages
((long_identifier) @function.builtin
 (#match? @function.builtin "^(raise|reraise|failwith|failwithf|invalidArg|invalidOp|nullArg)$"))

; base and fixed are reserved keywords but appear as plain identifiers in the tree
((identifier) @keyword (#match? @keyword "^(base|fixed)$"))

; CE bang-binding names
(ce_let_bang_expr name: (identifier) @variable)
(ce_use_bang_expr name: (identifier) @variable)
(use_binding name: (identifier) @variable)
