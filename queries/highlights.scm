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
  "let!" "do!" "use!" "and!"
  "get" "set" "and"
  "lazy" "assert"
  "begin" "end"
  "function"
  "delegate"
  "struct"
  "class"
] @keyword

[
  "private"
  "internal"
  "public"
] @keyword.control.access

["not" "upcast" "downcast" "nameof" "sizeof" "typeof" "typedefof"] @keyword.operator

(address_of_expression "&" @operator)
(optional_named_arg "?" @operator)

(type_constraint ["null" "struct" "comparison" "equality" "unmanaged" "enum" "delegate"] @keyword)
(type_constraint "not" @keyword.operator)

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

(typed_quotation "<@" @punctuation.special)
(typed_quotation "@>" @punctuation.special)
(untyped_quotation "<@@" @punctuation.special)
(untyped_quotation "@@>" @punctuation.special)

[
  ";"
  ","
] @punctuation.delimiter

(preproc_keyword) @keyword.directive
(preproc_if_kw) @keyword.directive
(preproc_elif_kw) @keyword.directive
(preproc_else_kw) @keyword.directive
(preproc_endif_kw) @keyword.directive
(shebang) @keyword.directive

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
(interpolated_string) @string
(interpolated_verbatim_string) @string
(interpolated_triple_string) @string
(interpolation "{" @punctuation.special)
(interpolation "}" @punctuation.special)
(interpolation (format_string) @string.special)
(interpolation (printf_format_string) @string.special)
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
  name: (operator_name) @function)

(let_decl_indented
  name: (active_pattern_name) @function)

(let_decl_indented
  name: (identifier) @function
  parameters: (parameter
    (identifier) @variable.parameter)*)

(let_decl_indented
  name: (operator_name) @function)

(let_and_binding
  name: (identifier) @function
  parameters: (parameter
    (identifier) @variable.parameter)*)

(lambda_expression
  (parameter
    (identifier) @variable.parameter)*)

; Capitalized non-last identifier in a long_identifier — likely a module or
; type segment in a dotted chain (e.g., `Async.FromContinuations`, where
; `Async` is the module). `#match?` ensures only PascalCase identifiers
; match, so `s.ToUpper` doesn't tint `s` as a type. Anchors: first child,
; immediately followed by another identifier (so the captured identifier is
; NOT last).
((long_identifier . (identifier) @type . (identifier))
 (#match? @type "^[A-Z]"))

; Last segment of a multi-segment long_identifier is a member access — e.g.
; `s.ToUpper` parses as long_identifier(s, ToUpper), and `ToUpper` is the
; member. Leading `.` says the first identifier must be the FIRST child;
; trailing `.` says the second identifier must be the LAST child. Between
; them, no anchor — so the second identifier can be at any position after
; the first, which is what catches 3+ segment chains like `Lib.Math.Integer`
; (captures only `Integer`). Single-identifier long_identifiers don't match
; because the pattern requires two identifiers.
;
; This rule sits BEFORE the type/namespace/attribute long_identifier
; captures. Those parent captures cover the whole long_identifier as
; @type/@namespace/@attribute, but tree-sitter's last-in-source-order
; resolution means the inner @variable.other.member here wins for the
; trailing identifier unless a more-specific override re-captures it.
; The explicit overrides further down re-apply @type / @namespace /
; @attribute to the trailing identifier in those contexts.
(long_identifier . (identifier) (identifier) @variable.other.member .)

; PascalCase receiver in dot_expression chains — `System.Threading.Interlocked`
; in `System.Threading.Interlocked.Increment(...)`. The OBJECT of a
; dot_expression is the receiver path, so capitalized identifiers in it are
; module/namespace/type segments rather than members. (The @type override
; for the nested member case is further down, after the @member captures.)
((dot_expression object: (long_identifier (identifier) @type))
 (#match? @type "^[A-Z]"))

; Named types anywhere in a type expression
(type_expression (long_identifier) @type)
(generic_type (long_identifier) @type)
(postfix_type (long_identifier) @type)
(type_parameter) @type.parameter

; Unit identifiers in measure types and literals
(measure_power_type (long_identifier) @type)
(measure_expression (long_identifier) @type)
(measure_expression (type_parameter) @type.parameter)

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

; Override: in type/namespace/attribute contexts, the previous rules tinted
; individual identifiers as @variable.other.member or @type (PascalCase
; heuristic). Re-capture EACH identifier as the more-specific @type /
; @namespace / @attribute so the whole long_identifier matches its
; containing context, not the expression-context heuristics.
; (Helix's highlight resolution picks the LAST capture in source order.)
(type_expression (long_identifier (identifier) @type))
(generic_type (long_identifier (identifier) @type))
(postfix_type (long_identifier (identifier) @type))
(measure_power_type (long_identifier (identifier) @type))
(measure_expression (long_identifier (identifier) @type))
(type_check_pattern (long_identifier (identifier) @type))
(attribute_target name: (long_identifier (identifier) @attribute))
(namespace_decl name: (long_identifier (identifier) @namespace))
(module_decl name: (long_identifier (identifier) @namespace))
(module_decl abbrev: (long_identifier (identifier) @namespace))
(import_decl (long_identifier (identifier) @namespace))
(new_expression (long_identifier (identifier) @type))
(new_expression (generic_type (long_identifier (identifier) @type)))
(object_expression type: (long_identifier (identifier) @type))
(object_expression type: (generic_type (long_identifier (identifier) @type)))

; module M = Lib  /  module M = Lib.Math.Integer  — abbreviation target
(module_decl abbrev: (long_identifier) @namespace)

(import_decl
  [
    (long_identifier) @namespace
  ])

(member_self_ident) @variable

(member_defn
  name: (identifier) @function)

(property_accessor (parameter (identifier) @variable.parameter))

(abstract_member_defn
  name: (identifier) @function)

(type_constraint member_name: (identifier) @function)
(type_constraint member_name: (operator_name) @function)

(val_field
  name: (identifier) @variable.other.member)

(type_decl
  name: (identifier) @type)

(type_extension_name (identifier) @type)


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

; Override: when the dot_expression is itself the OBJECT of another
; dot_expression (i.e., NOT the outermost call), the captured member is a
; type/namespace segment, not the called method. PascalCase filter avoids
; tinting lowercase fields. For `System.Threading.Interlocked.Increment(...)`,
; this re-tints `Interlocked` as @type after the previous rule made it
; @variable.other.member.
((dot_expression
   object: (dot_expression member: (identifier) @type))
 (#match? @type "^[A-Z]"))

; DU constructors and active-pattern cases in match patterns.
; Capitalized identifier in identifier_pattern position = constructor (F# convention).
((identifier_pattern
   (long_identifier (identifier) @constructor))
 (#match? @constructor "^[A-Z]"))

; Named DU field pattern: Email(address = addr) → highlight the constructor name
(named_field_pattern constructor: (long_identifier) @constructor)

; Type name in new expressions (not wrapped in type_expression so needs its own capture)
(new_expression (long_identifier) @type)
(new_expression (generic_type (long_identifier) @type))

; Type name in object expressions { new IFoo with … } / { new Base(arg) with … }
(object_expression type: (long_identifier) @type)
(object_expression type: (generic_type (long_identifier) @type))

; Computation expression builder name (async, task, seq, promise, …)
(computation_expression
  builder: (long_identifier) @keyword)

; Query CE custom operators (select/where/groupBy/join/...). The leading keyword
; is captured by `op:` on the simple query_operator; compound forms use literal
; keywords inside the rule, so we color them by the literal text.
(query_operator op: _ @keyword.control)
(query_join_operator
  ["join" "in" "on"] @keyword.control)
(query_group_by_operator
  ["groupBy" "groupValBy" "groupJoin" "into"] @keyword.control)
(query_left_outer_join_operator
  ["leftOuterJoin" "in" "on" "into"] @keyword.control)

; Type name inside `nameof` — `nameof System.Math` highlights `System.Math` as
; a type, while `nameof xxx` (camelCase value) stays plain. Same `^[A-Z]` guard
; as the raise rule below.
; Per-identifier capture (not just the whole long_identifier) so the
; PascalCase / @variable.other.member heuristics earlier are overridden.
((nameof_expression
   (long_identifier (identifier) @type))
 (#match? @type "^[A-Z]"))

; Exception type after `raise`/`reraise`. Three shapes:
;   raise (MyError args)           — constructor call inside parens
;   raise (MyError)                — no-arg constructor in parens
;   raise MyError                  — no parens
; The `^[A-Z]` guard on the captured identifier avoids false-positives on
; `raise myVar` (variable holding an exception). A PascalCase-named variable
; would still false-positive, but that fights F# naming convention.
;
; These patterns come BEFORE the @function.builtin pattern below so the
; `raise` long_identifier ends up with @function.builtin colour — Helix
; uses the last matching capture, and capturing `raise` here as
; @function.builtin (no theme colour) would otherwise wipe out the builtin tint.
((application_expression
   (long_identifier) @function.builtin
   (parenthesized_expression
     (application_expression
       (long_identifier) @type)))
 (#match? @function.builtin "^(raise|reraise)$")
 (#match? @type "^[A-Z]"))

((application_expression
   (long_identifier) @function.builtin
   (parenthesized_expression
     (long_identifier) @type))
 (#match? @function.builtin "^(raise|reraise)$")
 (#match? @type "^[A-Z]"))

((application_expression
   (long_identifier) @function.builtin
   (long_identifier) @type)
 (#match? @function.builtin "^(raise|reraise)$")
 (#match? @type "^[A-Z]"))

; Exception-raising functions — highlighted like throw/raise in other languages
((long_identifier) @function.builtin
 (#match? @function.builtin "^(raise|reraise|failwith|failwithf|invalidArg|invalidOp|nullArg)$"))

; base and fixed are reserved keywords but appear as plain identifiers in the tree
((identifier) @keyword (#match? @keyword "^(base|fixed)$"))

; CE bang-binding names
(ce_let_bang_expr name: (identifier) @variable)
(ce_use_bang_expr name: (identifier) @variable)
(use_binding name: (identifier) @variable)
(use_expression name: (identifier) @variable)
