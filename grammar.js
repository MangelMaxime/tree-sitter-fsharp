/**
 * @file F# tree sitter definition focused on Helix
 * @author Mangel Maxime
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const TYPE_PREC = {
    FUNCTION: 1,   // int -> string   (right-assoc, lowest)
    TUPLE:    2,   // int * string
    POSTFIX:  3,   // int list, int option  (left-assoc)
    APP:      4,   // list<int>, int[]  (highest)
};

const PREC = {
    SEQ_EXPR:      1,
    PIPE_EXPR:     1,   // |>, <|, >>, <<
    THEN_EXPR:     2,
    BOOL_OR:       2,   // ||
    RARROW:        3,
    BOOL_AND:      3,   // &&
    INFIX_OP:      4,   // comparison: = <> < > <= >=
    ADDITIVE:      5,   // + -
    MULTIPLICATIVE:6,   // * / %
    LET_DECL:      7,
    DO_EXPR:       8,
    FUN_EXPR:      1,   // low so the body $._expression expands greedily
    MATCH_EXPR:    8,
    MATCH_DECL:    9,
    DO_DECL:       10,
    ELSE_EXPR:     11,
    INTERFACE:     12,
    COMMA:         13,
    INFIX_OR:      13,
    INFIX_AND:     14,
    PREFIX_EXPR:   15,
    APP_EXPR:      16,
    SPECIAL_INFIX: 16,
    LARROW:        16,
    TUPLE_EXPR:    1,    // below BOOL_OR so OR binds tighter than comma
    CE_EXPR:       15,
    SPECIAL_PREFIX:17,
    IF_EXPR:       14,
    DOT:           19,
    INDEX_EXPR:    20,
    PAREN_APP:     21,
    PAREN_EXPR:    21,
    TYPED_EXPR:    22,
    DOTDOT:        22,
    DOTDOT_SLICE:  23,
    NEW_OBJ:       24,
    LET_EXPR:      1,    // low so the in-body $._expression expands greedily
};

export default grammar({
    name: "fsharp",

    word: $ => $.identifier,

    externals: $ => [
        // _body_indent/_body_dedent: wrap let_binding bodies (checked first by scanner).
        // _indent/_dedent: wrap let_decl_indented bodies inside let_expression.
        // Listing _body_indent before _indent ensures the scanner emits _body_indent when both
        // are valid, which kills the let_decl_indented path so let_binding wins at module level.
        $._body_indent,
        $._body_dedent,
        $._indent,
        $._dedent,
    ],

    extras: $ => [/\s+/, $.xml_doc_comment, $.line_comment, $.block_comment, $.block_doc_comment],

    // Without these declarations prec.dynamic is silently ignored.
    // record_type_field vs postfix_type: after `type_expression`, `long_identifier` could
    //   extend to postfix_type OR be the name of the next field.
    // record_field vs application_expression: after a value expression, a bare identifier
    //   could be a function argument OR the name of the next field.
    conflicts: $ => [
        [$.record_type_field, $.postfix_type],
        [$.record_field, $.application_expression],
        // Named union field type vs anonymous union field type: after 'name: 'a' or 'name: T',
        // '*' could start the next named field OR extend the type into a tuple_type.
        [$._union_field_type, $.type_expression],
        // measure_expression and type_expression both match long_identifier and type_parameter,
        // so GLR explores both when the parser sees a single atom in generic type args or aliases.
        [$.measure_expression, $.type_expression],
        // After "type identifier", both type_decl and type_extension_name are viable.
        // GLR explores both; "=" resolves to type_decl, "with" resolves to type_extension.
        [$.type_decl, $.type_extension_name],
        // After "module name =", an identifier could start the abbrev field (module abbreviation)
        // or be the first token of the next _token (nested module body). GLR explores both;
        // keyword identifiers fail the abbrev path, plain names succeed the abbreviation path.
        [$.module_decl],
    ],


    rules: {
        source_file: $ => repeat($._token),

        import_decl: ($) => seq("open", optional("type"), $.long_identifier),

        // namespace Foo.Bar  or  namespace global
        namespace_decl: $ => seq(
            "namespace",
            optional("rec"),
            choice("global", field('name', $.long_identifier)),
        ),

        // module Foo.Bar                    (file-level / abbreviated module)
        // module [private|internal] Foo =   (explicit nested module header; body is flat tokens)
        // module M = Lib                    (module abbreviation — target captured as abbrev field)
        // module M = Lib.Math.Integer       (qualified abbreviation target)
        module_decl: $ => seq(
            "module",
            optional($.access_modifier),
            optional("rec"),
            field('name', $.long_identifier),
            optional(seq("=", optional(field('abbrev', $.long_identifier)))),
        ),

        access_modifier: _ => choice("private", "internal", "public"),

        // [<EntryPoint>]  [<Obsolete("msg")>]  [<DllImport("lib", EntryPoint="f")>]
        // [<Attr1; Attr2>] (multiple in one bracket)
        attribute: $ => seq(
            "[<",
            $.attribute_target,
            repeat(seq(";", $.attribute_target)),
            ">]",
        ),

        attribute_target: $ => seq(
            field('name', $.long_identifier),
            optional(seq(
                "(",
                optional(seq($._expression, repeat(seq(",", $._expression)))),
                ")",
            )),
        ),


        // type Point = { X: int; Y: int }
        // type Shape = | Circle of float | Rectangle of float * float
        // type Option<'a> = | Some of 'a | None
        // type Foo(x: int) =        (class with primary constructor; body is flat top-level tokens)
        // type IFoo =               (interface; body is flat abstract_member_defn tokens)
        type_decl: $ => prec.right(seq(
            "type",
            field('name', $.identifier),
            optional($.type_parameter_list),
            optional($.tuple_params),
            // "=" is optional: [<Measure>] type kg has no body.
            // Class/interface bodies are also empty (members appear as flat _token siblings).
            optional(seq(
                "=",
                optional(choice($.record_type_defn, $.union_type_defn, $.enum_type_defn, $.delegate_type_defn, $.struct_type_defn, field('alias', $.measure_expression), prec.dynamic(1, field('alias', $.type_expression)))),
            )),
            repeat($.type_and_decl),
        )),


        // and Even = ...  (mutual type recursion continuation)
        type_and_decl: $ => prec.right(seq(
            "and",
            field('name', $.identifier),
            optional($.type_parameter_list),
            optional($.tuple_params),
            optional(seq(
                "=",
                optional(choice($.record_type_defn, $.union_type_defn, $.enum_type_defn, $.delegate_type_defn, $.struct_type_defn, field('alias', $.measure_expression), prec.dynamic(1, field('alias', $.type_expression)))),
            )),
        )),

        // type Foo with              — simple (intrinsic) extension
        // type Foo<'T> with          — generic extension
        // type System.String with    — optional (external) extension
        // Body members appear as flat _token siblings (same as class bodies).
        //
        // A named node for the type extension name avoids the issue where an
        // anonymous choice(seq(...)) propagates multiple "name:" labels to child
        // identifiers, causing query `name: (identifier)` to only match the first.
        // With a named rule, `field('name', $.type_extension_name)` produces a single
        // "name:" field, and `(type_extension_name (identifier))` captures all idents.
        //
        // LALR disambiguation from type_decl:
        //   - Qualified (dot-separated): type_decl fails immediately at "."
        //   - Simple: disambiguated by "with" vs "=" after optional type_parameter_list
        type_extension: $ => seq(
            "type",
            field('name', $.type_extension_name),
            optional($.type_parameter_list),
            "with",
        ),

        type_extension_name: $ => choice(
            seq($.identifier, repeat1(seq(".", $.identifier))),  // qualified: System.String
            $.identifier,                                          // simple: Foo
        ),

        // Parenthesised comma-separated parameter group — the OOP/tuple calling convention.
        // Used in: type Foo(x: int, y: int) =    new(x) = Foo(x, 0)    member this.Add(x, y) = …
        // Each element may be optional (?name) and/or typed (name: type).
        tuple_params: $ => seq(
            "(",
            optional(seq(
                $.tuple_param,
                repeat(seq(",", $.tuple_param)),
            )),
            ")",
        ),

        tuple_param: $ => seq(
            optional("?"),
            $.identifier,
            optional(seq(":", $.type_expression)),
        ),

        // new(params) = expr [then expr]  — secondary (additional) constructor inside a class body.
        // Starts with bare "(" so it can't be confused with new_expression (which needs a type name
        // between "new" and "(").  The optional `then` clause runs side-effects after delegation.
        secondary_constructor: $ => prec.right(seq(
            optional($.access_modifier),
            "new",
            field('parameters', $.tuple_params),
            "=",
            $._expression,
            optional(seq("then", $._expression)),
        )),

        // member this.Name = expr
        // member this.Method arg : RetType = expr
        // static member Name args = expr
        // override this.ToString() = expr
        // member this.Prop with get() = expr
        // member this.Prop with get() = expr and set(v) = expr
        // member val AutoProp = expr with get, set
        member_defn: $ => choice(
            seq(
                choice("member", "override", "default"),
                optional("inline"),
                field('self', $.member_self_ident),
                ".",
                field('name', $.identifier),
                field('parameters', repeat($.parameter)),
                optional(seq(":", field('return_type', $.type_expression))),
                "=",
                $._expression,
            ),
            seq(
                "static",
                optional("inline"),
                "member",
                field('name', $.identifier),
                field('parameters', repeat($.parameter)),
                optional(seq(":", field('return_type', $.type_expression))),
                "=",
                $._expression,
            ),
            // instance property with get/set accessor
            seq(
                choice("member", "override", "default"),
                optional("inline"),
                field('self', $.member_self_ident),
                ".",
                field('name', $.identifier),
                "with",
                $.property_accessor,
                optional(seq("and", $.property_accessor)),
            ),
            // static property with get/set accessor
            seq(
                "static",
                optional("inline"),
                "member",
                field('name', $.identifier),
                "with",
                $.property_accessor,
                optional(seq("and", $.property_accessor)),
            ),
            // auto-property: member val Name [: type] = expr [with get [, set]]
            seq(
                "member",
                "val",
                field('name', $.identifier),
                optional(seq(":", field('return_type', $.type_expression))),
                "=",
                $._expression,
                optional($.auto_property_accessors),
            ),
            // static auto-property
            seq(
                "static",
                "member",
                "val",
                field('name', $.identifier),
                optional(seq(":", field('return_type', $.type_expression))),
                "=",
                $._expression,
                optional($.auto_property_accessors),
            ),
        ),

        // get() = expr  or  set(v) = expr  (inside a property definition)
        property_accessor: $ => seq(
            choice("get", "set"),
            field('parameters', repeat($.parameter)),
            "=",
            $._expression,
        ),

        // with get [, set]  (auto-property accessor list)
        auto_property_accessors: _ => seq(
            "with",
            choice("get", "set"),
            optional(seq(",", choice("get", "set"))),
        ),

        member_self_ident: $ => $.identifier,

        // abstract member Name: TypeExpr
        // abstract member Prop: int with get, set
        abstract_member_defn: $ => seq(
            optional("static"),
            "abstract",
            optional("member"),
            field('name', $.identifier),
            ":",
            $.type_expression,
        ),

        // inherit BaseClass(arg1, arg2)
        inherit_decl: $ => prec.right(seq(
            "inherit",
            field('base', $.type_expression),
            optional(seq(
                "(",
                optional(seq($._expression, repeat(seq(",", $._expression)))),
                ")",
            )),
        )),

        // interface IFoo with   (member impls follow as flat tokens)
        interface_impl: $ => seq(
            "interface",
            field('type', $.type_expression),
            optional("with"),
        ),

        // do expr  (class initializer or module-level side effect)
        // static do runs once at type initialization time
        do_stmt: $ => seq(optional("static"), "do", $._expression),

        // val mutable field: int  (explicit field in class)
        val_field: $ => seq(
            "val",
            optional("mutable"),
            field('name', $.identifier),
            ":",
            $.type_expression,
        ),

        union_type_defn: $ => repeat1($.union_case),

        union_case: $ => seq(
            "|",
            field('name', $.identifier),
            optional(seq("of", choice(
                $.union_case_named_fields,
                field('fields', $.type_expression),
            ))),
        ),

        // | Circle of radius: float  or  | Rect of width: float * height: float
        union_case_named_fields: $ => seq(
            $.union_case_field,
            repeat(seq("*", $.union_case_field)),
        ),

        union_case_field: $ => seq(
            field('name', $.identifier),
            ":",
            field('type', $._union_field_type),
        ),

        // Type allowed inside a named union field.
        // Excludes tuple_type so that '*' between fields is never mistaken for
        // a tuple-type separator inside the previous field's type annotation.
        _union_field_type: $ => choice(
            $.function_type,
            $.postfix_type,
            $.generic_type,
            $.array_type,
            $.parenthesized_type,
            $.anonymous_record_type,
            $.type_parameter,
            $.long_identifier,
        ),

        // type Color = | Red = 0 | Green = 1 | Blue = 2
        enum_type_defn: $ => repeat1($.enum_case),

        // type Point3D = struct val x: float … end
        // Body is a flat repeat of _token (same approach as class bodies).
        // "end" is only lexed as a keyword once this rule exists in the grammar.
        struct_type_defn: $ => seq("struct", repeat($._token), "end"),

        // type MyDelegate = delegate of int -> string
        // type Handler = delegate of (obj * EventArgs) -> unit
        delegate_type_defn: $ => seq(
            "delegate",
            "of",
            field('arg_type', $.type_expression),
            "->",
            field('return_type', $.type_expression),
        ),

        enum_case: $ => seq(
            "|",
            field('name', $.identifier),
            "=",
            field('value', choice($.int_literal, $.negative_literal, $.char_literal)),
        ),

        // {| Name: string; Age: int |}  (anonymous record type expression)
        anonymous_record_type: $ => seq(
            "{|",
            $.record_type_field,
            repeat(prec.dynamic(TYPE_PREC.POSTFIX + 1, seq(optional(";"), $.record_type_field))),
            optional(";"),
            "|}",
        ),

        record_type_defn: $ => seq(
            "{",
            $.record_type_field,
            // prec.dynamic > TYPE_PREC.POSTFIX: when the GLR explores both "extend type via
            // postfix_type" and "start next field", prefer starting the next field.
            repeat(prec.dynamic(TYPE_PREC.POSTFIX + 1, seq(optional(";"), $.record_type_field))),
            optional(";"),
            "}",
        ),

        // prec(TYPE_PREC.POSTFIX) ties the REDUCE precedence with postfix_type's SHIFT precedence,
        // creating a genuine LR(1) conflict that the conflicts+prec.dynamic machinery can resolve.
        record_type_field: $ => prec(TYPE_PREC.POSTFIX, seq(
            optional("mutable"),
            field('name', $.identifier),
            ":",
            field('type', $.type_expression),
        )),

        _literal: $ => choice(
            $.measure_literal,
            $.int_literal,
            $.float_literal,
            $.char_literal,
            $.string_literal,
            $.verbatim_string,
            $.triple_quoted_string,
            $.interpolated_string,
            $.interpolated_verbatim_string,
            $.interpolated_triple_string,
            $.bool_literal,
            $.unit,
            $.null_literal,
        ),

        _expression: $ => prec(PREC.RARROW, choice(
            $.parenthesized_expression,
            $.typed_expression,
            $.application_expression,
            $.binary_expression,
            $.unary_expression,
            $.list_expression,
            $.array_expression,
            $.record_expression,
            $.anonymous_record_expression,
            $.tuple_expression,
            $._literal,
            $.long_identifier,
            $.if_expression,
            $.match_expression,
            $.lambda_expression,
            $.let_expression,
            $.use_expression,
            $.computation_expression,
            $.for_expression,
            $.while_expression,
            $.dot_expression,
            $.index_expression,
            $.try_expression,
            $.prefix_keyword_expression,
            $.begin_end_expression,
            $.function_expression,
            $.typecast_expression,
            $.keyword_cast_expression,
            $.nameof_expression,
            $.new_expression,
            $.object_expression,
            // CE result forms — also valid in if/match branches inside CEs
            $.ce_result_expr,
            $.struct_tuple_expression,
            $.typed_quotation,
            $.untyped_quotation,
            $.optional_named_arg,
            $.address_of_expression,
            $.type_keyword_expression,
        )),

        // struct (a, b)  struct (a, b, c)
        struct_tuple_expression: $ => prec(PREC.PAREN_EXPR, seq(
            "struct",
            "(",
            $._expression,
            ",",
            $._expression,
            repeat(seq(",", $._expression)),
            ")",
        )),

        // <@ expr @>   — typed quotation (Expr<'T>)
        typed_quotation: $ => prec(PREC.PAREN_EXPR, seq("<@", $._expression, "@>")),

        // <@@ expr @@>  — untyped quotation (Expr)
        untyped_quotation: $ => prec(PREC.PAREN_EXPR, seq("<@@", $._expression, "@@>")),

        // ?identifier — optional named argument reference  f(?name = Some value)
        optional_named_arg: $ => seq("?", $.identifier),

        // &expr — address-of / byref argument  someFunc(&mutableVal)
        address_of_expression: $ => prec(PREC.PREFIX_EXPR, seq("&", $._expression)),

        // sizeof<'T>  typeof<'T>  typedefof<'T> — type-level intrinsics
        type_keyword_expression: $ => seq(
            choice("sizeof", "typeof", "typedefof"),
            "<",
            $.type_expression,
            ">",
        ),

        parenthesized_expression: $ => seq("(", $._expression, ")"),

        // (expr : type)  — inline type annotation, always parenthesised.
        // Disambiguated from parenthesized_expression by the ":" after the expression.
        typed_expression: $ => seq("(", $._expression, ":", $.type_expression, ")"),

        // Second argument is restricted to simple expressions (no let/if/match/lambda/operators)
        // so that `let x = 1\nlet y = 2` doesn't try to parse the second `let` as an argument.
        application_expression: $ => prec.left(PREC.APP_EXPR, seq(
            $._expression,
            $._simple_expression,
        )),

        _simple_expression: $ => choice(
            $.parenthesized_expression,
            $.typed_expression,
            $.list_expression,
            $.array_expression,
            $.record_expression,
            $.anonymous_record_expression,
            $.measure_literal,
            $.int_literal,
            $.float_literal,
            $.char_literal,
            $.string_literal,
            $.verbatim_string,
            $.triple_quoted_string,
            $.interpolated_string,
            $.interpolated_verbatim_string,
            $.interpolated_triple_string,
            $.bool_literal,
            $.unit,
            $.null_literal,
            $.long_identifier,
            $.object_expression,
        ),

        // All infix binary operations collapsed into one rule to reduce post-_expression state bloat.
        // Each alternative carries its own prec so shift/reduce between operators still resolves correctly.
        binary_expression: $ => choice(
            prec.left(PREC.PIPE_EXPR,      seq($._expression, choice("|>", "<|", ">>", "<<"), $._expression)),
            prec.left(PREC.BOOL_OR,        seq($._expression, "||", $._expression)),
            prec.left(PREC.BOOL_AND,       seq($._expression, "&&", $._expression)),
            prec.left(PREC.ADDITIVE,       seq($._expression, choice("+", "-"), $._expression)),
            prec.left(PREC.MULTIPLICATIVE, seq($._expression, choice("*", "/", "%"), $._expression)),
            prec.left(PREC.INFIX_OP,       seq($._expression, choice(">", "<", ">=", "<=", "=", "<>"), $._expression)),
            prec.right(PREC.INFIX_OP,      seq($._expression, "::", $._expression)),
            prec.left(PREC.INFIX_OP,       seq($._expression, $.symbolic_op, $._expression)),
            prec.right(PREC.LARROW,        seq($._expression, "<-", $._expression)),
            prec.right(1,                  seq($._expression, "..", $._expression)),
        ),

        lambda_expression: $ => prec.right(PREC.FUN_EXPR,
            seq(
                "fun",
                repeat1($.parameter),
                "->",
                $._expression,
            ),
        ),

        unary_expression: $ => prec(PREC.PREFIX_EXPR, seq(
            choice("not", "~~~", "-", "!"),
            $._expression,
        )),

        symbolic_op: _ => token(/[!$%&*+\-\/<=>?@^|][!$%&*+\/<=>?@^|~]*/),

        list_expression: $ => seq(
            "[",
            optional(seq(
                $._expression,
                repeat(seq(";", $._expression)),
            )),
            "]",
        ),

        array_expression: $ => seq(
            "[|",
            optional(seq(
                $._expression,
                repeat(seq(";", $._expression)),
            )),
            "|]",
        ),

        // { x = 1; y = 2 }  or  { r with x = 1 }  or newline-separated (no semicolons)
        // Base of copy-update is restricted to _simple_expression to avoid ambiguity with
        // record_field (which also starts with long_identifier "="). Complex bases need parens.
        // prec.dynamic(APP_EXPR + 1) on the repeat body resolves the GLR conflict: when `Y`
        // could either start a new field or be consumed as a function argument to the previous
        // field's value, prefer starting a new field.
        // {| Name = "Alice"; Age = 30 |}  or  {| r with Name = "Bob" |}
        anonymous_record_expression: $ => seq(
            "{|",
            choice(
                seq(
                    field('base', $._simple_expression),
                    "with",
                    $.record_field,
                    repeat(prec.dynamic(PREC.APP_EXPR + 1, seq(optional(";"), $.record_field))),
                    optional(";"),
                ),
                seq(
                    $.record_field,
                    repeat(prec.dynamic(PREC.APP_EXPR + 1, seq(optional(";"), $.record_field))),
                    optional(";"),
                ),
            ),
            "|}",
        ),

        record_expression: $ => seq(
            "{",
            choice(
                seq(
                    field('base', $._simple_expression),
                    "with",
                    $.record_field,
                    repeat(prec.dynamic(PREC.APP_EXPR + 1, seq(optional(";"), $.record_field))),
                    optional(";"),
                ),
                seq(
                    $.record_field,
                    repeat(prec.dynamic(PREC.APP_EXPR + 1, seq(optional(";"), $.record_field))),
                    optional(";"),
                ),
            ),
            "}",
        ),

        // prec(APP_EXPR) ties REDUCE precedence with application_expression's SHIFT precedence so
        // prec.dynamic in the repeat body can prefer a new field over extending the value expression.
        record_field: $ => prec(PREC.APP_EXPR, seq(
            field('name', $.long_identifier),
            "=",
            field('value', $._expression),
        )),

        tuple_expression: $ => prec.left(PREC.TUPLE_EXPR, seq(
            $._expression,
            ",",
            $._expression,
            repeat(seq(",", $._expression)),
        )),

        if_expression: $ => prec.right(PREC.IF_EXPR,
            seq(
                "if",
                $._expression,
                "then",
                $._expression,
                repeat(seq("elif", $._expression, "then", $._expression)),
                optional(seq("else", $._expression)),
            ),
        ),

        // let (>>=) a b = ...  — operator definition name in parens
        operator_name: $ => seq("(", $.symbolic_op, ")"),

        let_binding: ($) => prec.right(PREC.LET_DECL,
            seq(
                optional("static"),
                "let",
                optional("rec"),
                optional(choice("inline", "mutable")),
                field('name', choice(
                    $.identifier, $.operator_name, $.active_pattern_name,
                    $.typed_pattern, $.tuple_pattern, $.struct_tuple_pattern, $.unparenthesized_tuple_pattern, $.record_pattern, $.list_pattern, $.array_pattern, $.wildcard_pattern,
                )),
                optional($.type_parameter_list),
                field('parameters', repeat($.parameter)),
                optional(seq(":", field('return_type', $.type_expression))),
                "=",
                choice(
                    seq($._body_indent, $._expression, $._body_dedent),
                    $._expression,
                ),
                repeat($.let_and_binding),
            ),
        ),

        // and name params [: type] = expr  (mutual recursion continuation)
        let_and_binding: ($) => prec.right(PREC.LET_DECL,
            seq(
                "and",
                optional(choice("inline", "mutable")),
                field('name', choice(
                    $.identifier, $.operator_name, $.active_pattern_name,
                    $.typed_pattern, $.tuple_pattern, $.struct_tuple_pattern, $.unparenthesized_tuple_pattern, $.record_pattern, $.list_pattern, $.array_pattern, $.wildcard_pattern,
                )),
                optional($.type_parameter_list),
                field('parameters', repeat($.parameter)),
                optional(seq(":", field('return_type', $.type_expression))),
                "=",
                $._expression,
            ),
        ),

        // "let x = \n    body" — body on next indented line, body has its own node.
        // Used as the binding part of let_expression when the body is indented.
        // The INDENT/DEDENT tokens are zero-width and emitted by the external scanner.
        let_decl_indented: ($) => seq(
            "let",
            optional("rec"),
            optional(choice("inline", "mutable")),
            field('name', choice(
                $.identifier, $.operator_name, $.active_pattern_name,
                $.typed_pattern, $.tuple_pattern, $.struct_tuple_pattern, $.unparenthesized_tuple_pattern, $.record_pattern, $.list_pattern, $.array_pattern, $.wildcard_pattern,
            )),
            optional($.type_parameter_list),
            field('parameters', repeat($.parameter)),
            optional(seq(":", field('return_type', $.type_expression))),
            "=",
            $._indent,
            field('body', $._expression),
            $._dedent,
        ),

        // let_expression:
        //   Branch A — indented body (let x = \n    body \n continuation)
        //              Scanner emits _indent/_dedent to scope the body.
        //   Branch B — explicit-in body (let x = expr in continuation)
        //              "in" is REQUIRED: without "in", indented bodies use branch A,
        //              and module-level lets use let_binding. This eliminates the
        //              3-way GLR conflict that caused state-count explosion.
        let_expression: ($) => prec.right(PREC.LET_EXPR,
            choice(
                // Branch A: indented body + continuation (scanner-delimited)
                seq(
                    field('binding', $.let_decl_indented),
                    field('continuation', $._expression),
                ),
                // Branch B: explicit "in" body
                seq(
                    "let",
                    optional("rec"),
                    optional(choice("inline", "mutable")),
                    field('name', choice(
                        $.identifier, $.operator_name, $.active_pattern_name,
                        $.typed_pattern, $.tuple_pattern, $.struct_tuple_pattern, $.unparenthesized_tuple_pattern, $.record_pattern, $.list_pattern, $.array_pattern, $.wildcard_pattern,
                    )),
                    optional($.type_parameter_list),
                    field('parameters', repeat($.parameter)),
                    optional(seq(":", field('return_type', $.type_expression))),
                    "=",
                    $._expression,
                    "in",
                    $._expression,
                ),
            ),
        ),

        // use r = resource   (in expression bodies; auto-disposes r at end of scope)
        // The body $._expression greedily absorbs the continuation when there is no `in`.
        use_expression: $ => prec.right(PREC.LET_EXPR,
            seq("use", field('name', $.identifier), "=", $._expression),
        ),

        // expr.Member  — member access on any expression that can't extend long_identifier.
        // long_identifier(prec.right DOT=19) wins the shift-reduce conflict at "."
        // when the LHS is a plain identifier, so A.B.C stays a single long_identifier.
        // dot_expression only fires when the LHS is already an _expression that cannot
        // be extended by long_identifier (e.g. index_expression, application_expression).
        dot_expression: $ => prec(PREC.DOT, seq(
            field('object', $._expression),
            ".",
            field('member', $.identifier),
        )),

        // arr.[0]  arr.[1..2]  arr.[..2]  arr.[1..]  dict.["k"]  m.[0, 1]
        // ".[" is a single terminal so the lexer never confuses it with the
        // "." in long_identifier (which is always followed by an identifier).
        index_expression: $ => prec(PREC.INDEX_EXPR, seq(
            field('object', $._expression),
            ".[",
            field('index', $._index_args),
            "]",
        )),

        _index_args: $ => seq(
            $._index_arg,
            repeat(seq(",", $._index_arg)),
        ),

        // index_slice vs binary_expression(".." alternative) both start with expr+"..".
        // prec.dynamic(DOTDOT_SLICE) on index_slice makes it preferred inside index args,
        // where binary_expression's range alternative would fail (no rhs before ] or ,).
        index_slice: $ => prec.dynamic(PREC.DOTDOT_SLICE, choice(
            seq($._expression, ".."),
            seq("..", $._expression),
        )),

        _index_arg: $ => choice(
            $.index_slice,
            $._expression,
        ),

        // ── Type casts ────────────────────────────────────────────────────────
        // expr :> Type   upcast;  expr :?> Type   downcast;  expr :? Type   type test
        typecast_expression: $ => prec(PREC.TYPED_EXPR,
            seq($._expression, choice(":>", ":?>", ":?"), $.type_expression),
        ),

        // upcast expr / downcast expr — keyword forms (type inferred by compiler)
        keyword_cast_expression: $ => seq(choice("upcast", "downcast"), $._expression),

        // nameof expr  — returns the string name of the identifier/member at compile time
        nameof_expression: $ => seq("nameof", $._simple_expression),

        // ── New object ────────────────────────────────────────────────────────
        // new TypeName(args)  or  new TypeName<T>(args)
        // Type is restricted to long_identifier/generic_type so that the opening
        // "(" is never consumed as a parenthesized_type.
        new_expression: $ => prec(PREC.NEW_OBJ,
            seq(
                "new",
                choice($.generic_type, $.long_identifier),
                "(",
                optional(seq($._expression, repeat(seq(",", $._expression)))),
                ")",
            ),
        ),

        // { new IFoo with member ... }  or  { new BaseClass(arg) with override ... }
        // "new" is a keyword so this never conflicts with record_expression.
        // Members are a flat repeat of member_defn / interface_impl, matching
        // the same flat structure used for type bodies.
        object_expression: $ => seq(
            "{",
            "new",
            field('type', choice($.generic_type, $.long_identifier)),
            optional(seq(
                "(",
                optional(seq($._expression, repeat(seq(",", $._expression)))),
                ")",
            )),
            optional(seq(
                "with",
                repeat(choice($.member_defn, $.interface_impl)),
            )),
            "}",
        ),

        // ── Exceptions ────────────────────────────────────────────────────────

        // exception MyErr  or  exception MyErr of string * int
        exception_decl: $ => seq(
            "exception",
            field('name', $.identifier),
            optional(seq("of", $.type_expression)),
        ),

        // try expr with | pat -> expr …   /   try expr finally expr
        try_expression: $ => prec.right(PREC.MATCH_EXPR, seq(
            "try", $._expression,
            choice(
                seq("with", repeat1($.match_arm)),
                seq("finally", $._expression),
            ),
        )),

        // lazy expr / assert expr — prefix keyword wrapping an expression
        prefix_keyword_expression: $ => prec(PREC.PREFIX_EXPR,
            seq(choice("lazy", "assert"), $._expression),
        ),

        // begin expr end  — sequenced block (equivalent to parenthesized)
        begin_end_expression: $ => prec(PREC.PAREN_EXPR, seq("begin", $._expression, "end")),

        // function | pat -> expr …  — shorthand for fun x -> match x with
        function_expression: $ => prec.right(PREC.MATCH_EXPR,
            seq("function", repeat1($.match_arm)),
        ),

        // ── For / While ───────────────────────────────────────────────────────

        // for x in xs do body   (foreach)  /  for i = start to/downto end do body  (range)
        for_expression: $ => prec.right(PREC.IF_EXPR, seq(
            "for",
            choice(
                seq(
                    choice($.identifier, $.wildcard_pattern, $.tuple_pattern, $.record_pattern),
                    "in", $._expression, "do", $._expression,
                ),
                seq(
                    $.identifier,
                    "=", $._expression, choice("to", "downto"), $._expression, "do", $._expression,
                ),
            ),
        )),

        // while cond do body   (imperative loop, returns unit)
        while_expression: $ => prec.right(PREC.IF_EXPR,
            seq("while", $._expression, "do", $._expression),
        ),

        // ── Computation expressions ────────────────────────────────────────
        // async { ... }  task { ... }  seq { ... }  promise { ... }
        // Builder name is any identifier; body is a flat sequence of CE statements.
        // Lower prec (CE_EXPR=15) than application_expression (APP_EXPR=16) so that
        // `f { field = val }` keeps the record-as-arg interpretation when both paths
        // are explored by GLR.
        computation_expression: $ => prec(PREC.CE_EXPR,
            seq(
                field('builder', $.long_identifier),
                "{",
                repeat($._ce_statement),
                "}",
            ),
        ),

        // A statement inside a computation expression body.
        // Inline rule: just a named alias for the choice.
        _ce_statement: $ => choice(
            $.ce_let_bang_expr,
            $.use_binding,
            $.ce_use_bang_expr,
            $.ce_match_bang_expr,
            $.let_binding,
            $.do_stmt,
            $._expression,  // covers return/yield/return!/yield!/do!/for/while/if/match/…
        ),

        // let! x = expr [and! y = expr ...]  — parallel applicative binding
        ce_let_bang_expr: $ => prec.right(PREC.LET_DECL,
            seq("let!", field('name', choice(
                $.identifier, $.typed_pattern, $.tuple_pattern, $.struct_tuple_pattern, $.unparenthesized_tuple_pattern, $.record_pattern, $.wildcard_pattern,
            )), "=", $._expression,
            repeat($.ce_and_bang_expr),
            ),
        ),

        // and! y = expr  — continuation of a parallel let! group
        ce_and_bang_expr: $ => prec.right(PREC.LET_DECL,
            seq("and!", field('name', choice(
                $.identifier, $.typed_pattern, $.tuple_pattern, $.struct_tuple_pattern, $.unparenthesized_tuple_pattern, $.record_pattern, $.wildcard_pattern,
            )), "=", $._expression),
        ),

        // use x = disposable  (also used as a top-level _token outside CEs)
        use_binding: $ => prec.right(PREC.LET_DECL,
            seq("use", field('name', $.identifier), "=", $._expression),
        ),

        // use! x = expr
        ce_use_bang_expr: $ => prec.right(PREC.LET_DECL,
            seq("use!", field('name', $.identifier), "=", $._expression),
        ),

        // match! expr with | pat -> expr …
        ce_match_bang_expr: $ => prec.right(PREC.MATCH_EXPR,
            seq("match!", $._expression, "with", repeat1($.match_arm)),
        ),

        // return/yield/do! result forms — in _expression so they work inside if/match branches in CEs
        ce_result_expr: $ => choice(
            seq("return", $._expression),
            seq("return!", $._expression),
            seq("yield", $._expression),
            seq("yield!", $._expression),
            seq("do!", $._expression),
        ),

        match_expression: ($) => prec.right(PREC.MATCH_EXPR,
            seq(
                "match",
                $._expression,
                "with",
                repeat1($.match_arm),
            ),
        ),

        match_arm: ($) => seq(
            "|",
            $.pattern,
            repeat(seq(",", $.pattern)),
            optional(seq("when", $._expression)),
            "->",
            $._expression,
        ),

        pattern: $ => choice(
            $.wildcard_pattern,
            $.literal_pattern,
            $.identifier_pattern,
            $.cons_pattern,
            $.or_pattern,
            $.tuple_pattern,
            $.struct_tuple_pattern,
            $.typed_pattern,
            $.as_pattern,
            $.list_pattern,
            $.array_pattern,
            $.type_check_pattern,
            $.record_pattern,
            $.named_field_pattern,
        ),

        // struct (a, b)  — destructure a struct tuple in match/let
        struct_tuple_pattern: $ => seq(
            "struct",
            "(",
            $.pattern,
            repeat(seq(",", $.pattern)),
            ")",
        ),

        // Constructor(field = pat; field2 = pat2)  — named DU field pattern
        // prec.dynamic(2) in repeat body: prefer starting a new field over
        // extending the previous field's pattern value via identifier_pattern.
        named_field_pattern: $ => seq(
            field('constructor', $.long_identifier),
            "(",
            $.named_field_pat,
            repeat(prec.dynamic(2, seq(optional(";"), $.named_field_pat))),
            optional(";"),
            ")",
        ),

        named_field_pat: $ => seq(
            field('name', $.identifier),
            "=",
            field('value', $.pattern),
        ),

        // pat1 | pat2  — alternative patterns (prec.left(1): binds tighter than `as` (0), looser than `::` (2))
        // Used both as nested sub-patterns and at the top level of match arms (replaces the old
        // repeat(seq("|", pattern)) in match_arm, which was ambiguous once or_pattern was in scope).
        or_pattern: $ => prec.left(1, seq($.pattern, "|", $.pattern)),

        // (pat : type)  — explicit type annotation on a pattern, always parenthesised.
        // Used in match arms and as function parameters.
        typed_pattern: $ => seq("(", $.pattern, ":", $.type_expression, ")"),

        // { Field = pat; Field2 = pat2 }  (destructure a record in a match arm)
        // prec.dynamic(2) in the repeat body: when a bare identifier follows a DU
        // constructor pattern value, prefer starting a new field (prec=2) over
        // extending the constructor's argument (identifier_pattern prec.right(1)).
        record_pattern: $ => seq(
            "{",
            $.record_field_pattern,
            repeat(prec.dynamic(2, seq(optional(";"), $.record_field_pattern))),
            optional(";"),
            "}",
        ),

        record_field_pattern: $ => seq(
            field('name', $.long_identifier),
            "=",
            field('value', $.pattern),
        ),

        // | :? TypeName [as x] ->   (type-test pattern in match arms)
        // Uses a restricted type (no function_type) so that "->" is not consumed
        // as a function-type arrow and remains available as the match-arm separator.
        // Function types in patterns need explicit parens: :? (int -> string)
        type_check_pattern: $ => prec.right(TYPE_PREC.POSTFIX + 1, seq(
            ":?",
            choice(
                $.generic_type,
                $.postfix_type,
                $.array_type,
                $.parenthesized_type,
                $.type_parameter,
                $.long_identifier,
            ),
            optional(seq("as", $.identifier)),
        )),

        // x :: rest  or  x :: y :: rest  (right-associative; prec 2 > or_pattern 1 > as_pattern 0)
        cons_pattern: $ => prec.right(2, seq($.pattern, "::", $.pattern)),

        wildcard_pattern: _ => "_",

        literal_pattern: $ => choice(
            $.int_literal,
            $.float_literal,
            $.negative_literal,
            $.char_literal,
            $.string_literal,
            $.bool_literal,
            $.unit,
            $.null_literal,
        ),

        // -1  -3.14   in pattern position (e.g. match arms)
        negative_literal: $ => seq("-", choice($.int_literal, $.float_literal)),

        identifier_pattern: $ => choice(
            $.long_identifier,
            prec.right(1, seq($.long_identifier, $.pattern)),
        ),

        tuple_pattern: $ => seq(
            "(",
            $.pattern,
            repeat(seq(",", $.pattern)),
            ")",
        ),

        // Restricted element for unparenthesized tuple patterns.
        // Excludes identifier_pattern's constructor-application form (long_identifier followed
        // by a pattern) which would greedily consume `add a b` in `let add a b = ...` before
        // the parser discovers there is no `,` to form a tuple. Uses long_identifier directly
        // for bare names; other forms are all unambiguously delimited by a leading token.
        _tuple_elem_pattern: $ => choice(
            $.long_identifier,
            $.wildcard_pattern,
            $.literal_pattern,
            $.tuple_pattern,
            $.struct_tuple_pattern,
            $.typed_pattern,
            $.record_pattern,
            $.list_pattern,
            $.array_pattern,
        ),

        // a, b  or  a, b, c — bare tuple pattern without outer parens.
        // Valid as the name in let/let!/and! bindings: let a, b = 1, 2
        // Not added to $.pattern to avoid conflicts in match arms, which already
        // handle comma-separated patterns via match_arm's own repeat.
        unparenthesized_tuple_pattern: $ => seq(
            $._tuple_elem_pattern,
            ",",
            $._tuple_elem_pattern,
            repeat(seq(",", $._tuple_elem_pattern)),
        ),

        as_pattern: $ => prec.right(seq(
            $.pattern,
            "as",
            $.identifier,
        )),

        list_pattern: $ => seq(
            "[",
            optional(seq(
                $.pattern,
                repeat(seq(";", $.pattern)),
            )),
            "]",
        ),

        array_pattern: $ => seq(
            "[|",
            optional(seq(
                $.pattern,
                repeat(seq(";", $.pattern)),
            )),
            "|]",
        ),

        parameter: $ => choice(
            $.identifier,
            $.unit,
            $.wildcard_pattern,
            $.tuple_params,                // (x: int, y: int) or (x: int) — OOP-style grouped params
            $.destructure_parameter,       // ((a,b): int*int)  ({X=x}: Point)
            $.tuple_pattern,               // (a, b)  (Some x)  (x)
            $.record_pattern,              // { X = x }
        ),

        // (pattern : type) where the inner pattern is a destructuring form, not a bare identifier.
        // Bare-identifier form (x: int) is covered by tuple_params to avoid conflict with
        // multi-param OOP signatures like (x: int, y: int).
        destructure_parameter: $ => seq(
            "(",
            choice(
                $.tuple_pattern,
                $.struct_tuple_pattern,
                $.record_pattern,
                $.wildcard_pattern,
            ),
            ":",
            $.type_expression,
            ")",
        ),

        // cm^3  m^-1  (measure type raised to a power)
        measure_power_type: $ => seq(
            $.long_identifier,
            "^",
            choice($.int_literal, $.negative_literal),
        ),

        // Compound measure expressions: m/s  kg*m/s^2  'u  1
        // Juxtaposition (kg m) is not supported — use explicit * instead.
        measure_expression: $ => choice(
            prec.left(1, seq($.measure_expression, "/", $.measure_expression)),
            prec.left(2, seq($.measure_expression, "*", $.measure_expression)),
            $.measure_power_type,
            $.int_literal,
            $.type_parameter,
            $.long_identifier,
        ),

        // 3.0<cm>  55.0<miles/hour>  3u<days>
        // token.immediate ensures no whitespace between the number and '<',
        // distinguishing measure literals from comparison expressions (1.0 < x).
        measure_literal: $ => seq(
            choice($.int_literal, $.float_literal),
            token.immediate("<"),
            $.measure_expression,
            ">",
        ),

        type_expression: $ => choice(
            $.function_type,
            $.tuple_type,
            $.struct_tuple_type,
            $.postfix_type,
            $.generic_type,
            $.array_type,
            $.parenthesized_type,
            $.anonymous_record_type,
            $.measure_power_type,
            $.type_parameter,
            $.long_identifier,
        ),

        // struct (int * string)  — value-type tuple type
        struct_tuple_type: $ => seq("struct", "(", $.tuple_type, ")"),

        // int -> string  (right-assoc: int -> string -> bool = int -> (string -> bool))
        function_type: $ => prec.right(TYPE_PREC.FUNCTION, seq(
            $.type_expression, "->", $.type_expression,
        )),

        // int * string  (flat: int * string * bool stays flat via repeat)
        tuple_type: $ => prec.right(TYPE_PREC.TUPLE, seq(
            $.type_expression,
            "*",
            $.type_expression,
            repeat(seq("*", $.type_expression)),
        )),

        // int list, int option  (left-assoc: int list option = (int list) option)
        postfix_type: $ => prec.left(TYPE_PREC.POSTFIX, seq(
            $.type_expression,
            $.long_identifier,
        )),

        // list<int>, Map<string, int>
        generic_type: $ => prec(TYPE_PREC.APP, seq(
            $.long_identifier,
            "<",
            choice(prec.dynamic(1, $.type_expression), $.measure_expression),
            repeat(seq(",", choice(prec.dynamic(1, $.type_expression), $.measure_expression))),
            ">",
        )),

        // int[]
        array_type: $ => prec(TYPE_PREC.APP, seq(
            $.type_expression,
            "[",
            "]",
        )),

        // (int -> string)
        parenthesized_type: $ => seq("(", $.type_expression, ")"),

        // 'a  ^T
        type_parameter: _ => token(seq(
            choice("'", "^"),
            /[a-zA-Z_][a-zA-Z0-9_']*/,
        )),

        // <'T, 'U when 'T :> IFoo and 'U : comparison>
        type_parameter_list: $ => seq(
            "<",
            $.type_parameter,
            repeat(seq(",", $.type_parameter)),
            optional(seq(
                "when",
                $.type_constraint,
                repeat(seq("and", $.type_constraint)),
            )),
            ">",
        ),

        // 'T :> IFoo   'T : null   'T : comparison   …
        type_constraint: $ => choice(
            seq($.type_parameter, ":>", $.type_expression),
            seq($.type_parameter, ":", "null"),
            seq($.type_parameter, ":", "struct"),
            seq($.type_parameter, ":", "not", "struct"),
            seq($.type_parameter, ":", "comparison"),
            seq($.type_parameter, ":", "equality"),
            seq($.type_parameter, ":", "unmanaged"),
            seq($.type_parameter, ":", "enum", "<", $.type_expression, ">"),
            seq($.type_parameter, ":", "delegate", "<", $.type_expression, ",", $.type_expression, ">"),
            seq($.type_parameter, ":", "(", "new", ":", "unit", "->", $.type_expression, ")"),
            // SRTP member constraint: ^T : (member Foo: int -> int)
            //                         ^T : (static member (+): ^T * ^T -> ^T)
            seq($.type_parameter, ":", "(",
                optional("static"),
                "member",
                field('member_name', choice($.identifier, $.operator_name)),
                ":",
                field('member_type', $.type_expression),
                ")"),
        ),

        // (|Even|Odd|)  (|Integer|_|)  (|Single|)
        // Single terminal so the lexer never splits "(|" as "(" then "|",
        // which would break operator definitions like let (|>) a b = …
        active_pattern_name: _ => token(seq(
            "(|",
            /[a-zA-Z_][a-zA-Z0-9_']*/,
            repeat(seq("|", /[a-zA-Z_][a-zA-Z0-9_']*/)),
            optional(seq("|", "_")),
            "|)",
        )),

        _token: $ => choice(
            $.preproc_if,
            $.preproc_directive,
            $.attribute,
            $.namespace_decl,
            $.module_decl,
            $.import_decl,
            $.type_decl,
            $.type_extension,
            $.exception_decl,
            $.let_binding,
            $.use_binding,
            $.member_defn,
            $.secondary_constructor,
            $.abstract_member_defn,
            $.interface_impl,
            $.inherit_decl,
            $.do_stmt,
            $.val_field,
            $.xml_doc_comment,
            $.line_comment,
            $.block_comment,
            $.block_doc_comment,
            $._expression,
        ),

        // keyword: _ => choice(...KEYWORDS),

        // Covers plain F# identifiers and backtick-quoted identifiers (``any text``),
        // unified in one terminal. token(choice(...)) is still a single terminal so
        // `word: $ => $.identifier` continues to work for keyword detection —
        // backtick forms never match keywords.
        identifier: _ => token(choice(
            /[a-zA-Z_][a-zA-Z0-9_']*/,
            /``[^`\n\r\t]+``/,
        )),

        // prec.right(DOT=19) beats the REDUCE of _expression (RARROW=3), so
        // identifier chains like A.B.C stay as a single long_identifier node
        // rather than being split into dot_expression(A.B, C) once dot_expression
        // is in scope.
        long_identifier: $ =>
            prec.right(PREC.DOT,
                seq(
                    $.identifier,
                    repeat(seq(".", $.identifier)),
                ),
            ),

        // Base number terminals (used by int_literal and float_literal)
        _int: _ => token(/[0-9][0-9_]*/),
        _hex: _ => token(seq(choice("0x", "0X"), /[0-9a-fA-F_]+/)),
        _oct: _ => token(seq(choice("0o", "0O"), /[0-7_]+/)),
        _bin: _ => token(seq(choice("0b", "0B"), /[01_]+/)),

        // Suffixes (immediate = no whitespace before suffix)
        // Note: We are allowing 'm', 'M', 'f', 'F' suffix even if they are not really for integer
        // This is in order to cover float declaration without a dot (3f, 42m, etc.)
        // Often themes don't make a difference in colors between integer and float, so it should work ok
        _int_suffix: _ => token.immediate(choice("uy", "us", "uL", "UL", "Ul", "ul", "un", "u", "y", "s", "l", "L", "n", "I", "m", "M", "f", "F")),
        _float_suffix: _ => token.immediate(choice("f", "F", "m", "M")),

        int_literal: $ => seq(
            choice($._hex, $._oct, $._bin, $._int),
            optional($._int_suffix),
        ),

        float_literal: $ => seq(
            choice(
                // Require at least one digit after the decimal point so that `1..10`
                // lexes as int(1) + ".." + int(10) rather than float(1.) + "." + int(10).
                token(seq(/[0-9][0-9_]*/, ".", /[0-9][0-9_]*/, optional(seq(/[eE]/, optional(/[+-]/), /[0-9]+/)))),
                token(seq(/[0-9]+/, /[eE]/, optional(/[+-]/), /[0-9]+/)),
            ),
            optional($._float_suffix),
        ),

        char_literal: _ => token(
            seq(
                "'",
                choice(
                    /[^'\\]/,
                    seq("\\", choice(
                        /[\\'"abfnrtv0]/,
                        /[0-9]{3}/,
                        /x[0-9a-fA-F]{2}/,
                        /u[0-9a-fA-F]{4}/,
                        /U[0-9a-fA-F]{8}/,
                    )),
                ),
                "'",
            )
        ),

        _string_content: _ => token(
            seq(
                '"',
                repeat(choice(
                    /[^"\\]+/,
                    seq("\\", choice(
                        /[\\'"abfnrtv0]/,
                        /[0-9]{3}/,
                        /x[0-9a-fA-F]{2}/,
                        /u[0-9a-fA-F]{4}/,
                        /U[0-9a-fA-F]{8}/,
                    )),
                )),
                '"',
            )
        ),

        _verbatim_string_content: _ => token(
            seq(
                '@"',
                repeat(choice(/[^"]+/, '""')),
                '"',
            )
        ),

        // Content segments for interpolated strings (token.immediate = no whitespace skip)
        // % is excluded so that printf-style %fmt{ specifiers are tokenised separately.
        _interp_string_text: _ => token.immediate(repeat1(choice(
            /[^"\\{}%]+/,
            /\\[\\'"abfnrtv0]/,
            /\\[0-9]{3}/,
            /\\x[0-9a-fA-F]{2}/,
            /\\u[0-9a-fA-F]{4}/,
            /\\U[0-9a-fA-F]{8}/,
            '{{',
            '}}',
        ))),

        _interp_verbatim_text: _ => token.immediate(repeat1(choice(
            /[^"{}%]+/,
            '""',
            '{{',
            '}}',
        ))),

        // "  + safe char avoids greedily consuming """ (the closing delimiter)
        _interp_triple_text: _ => token.immediate(repeat1(choice(
            /[^"{}%]+/,
            /""[^"{}%]/,
            /"[^"{}%]/,
            '{{',
            '}}',
        ))),

        // Everything after : inside {expr:fmt} until the closing }
        _interp_format_spec: _ => token.immediate(/[^}]+/),

        // Fallback for a literal % that is NOT the start of a valid printf format spec
        _interp_percent: _ => token.immediate('%'),

        // Printf-style format prefix: %[flags][width][.precision]conv{
        // The { is included so this token only matches when a hole immediately follows,
        // which prevents false positives like "100% done".
        _printf_format: _ => token.immediate(/%[-+ #0]*[0-9]*(?:\.[0-9]+)?[A-Za-z]\{/),

        _string_byte_suffix: _ => token.immediate("B"),

        // "hello"  or  "hello"B
        string_literal: $ => seq($._string_content, optional($._string_byte_suffix)),

        // @"hello"  or  @"hello"B
        verbatim_string: $ => seq($._verbatim_string_content, optional($._string_byte_suffix)),

        // """hello"""  — no byte variant in F#
        triple_quoted_string: _ => token(
            seq(
                '"""',
                repeat(choice(/[^"]+/, /"[^"]/, /""[^"]/)),
                '"""',
            )
        ),

        // {expr}, {expr:.NET-fmt}, or %printf-fmt{expr}
        interpolation: $ => choice(
            // %fmt{expr}  — printf-style; _printf_format includes the opening {
            seq(
                alias($._printf_format, $.printf_format_string),
                $._expression,
                '}',
            ),
            // {expr}  or  {expr:.NET-fmt}
            seq(
                '{',
                $._expression,
                optional(seq(':', alias($._interp_format_spec, $.format_string))),
                '}',
            ),
        ),

        // $"text %fmt{expr} {expr:.NET-fmt} text"
        interpolated_string: $ => seq(
            '$"',
            repeat(choice(
                $.interpolation,
                alias($._interp_string_text, $.string_content),
                alias($._interp_percent, $.string_content),
            )),
            '"',
        ),

        // $@"verbatim %fmt{expr} {expr}"  or  @$"..."
        interpolated_verbatim_string: $ => seq(
            choice('$@"', '@$"'),
            repeat(choice(
                $.interpolation,
                alias($._interp_verbatim_text, $.string_content),
                alias($._interp_percent, $.string_content),
            )),
            '"',
        ),

        // $"""triple %fmt{expr} {expr}"""
        interpolated_triple_string: $ => seq(
            '$"""',
            repeat(choice(
                $.interpolation,
                alias($._interp_triple_text, $.string_content),
                alias($._interp_percent, $.string_content),
            )),
            '"""',
        ),

        bool_literal: _ => choice("true", "false"),

        unit: _ => token(seq("(", ")")),

        null_literal: _ => token("null"),

        line_comment: _ => token(seq("//", choice(/[^/].*/, ""))),

        xml_doc_comment: _ => token(seq("///", /.*/)),

        // Non-nesting block comment. Nested comments (* (* inner *) *) are not supported —
        // the outer comment closes at the first *). This is a pragmatic tradeoff: making
        // block_comment a single token() removes the recursive grammar rule from extras,
        // which otherwise inflated every parser state's item set.
        block_comment: _ => token(seq("(*", /([^*]|\*+[^)*])*\*+/, ")")),

        // Doc comment: starts with (**. prec(1) wins over block_comment when both match the
        // same length (e.g. "(** doc *)" matches both; prec(1) selects block_doc_comment).
        block_doc_comment: _ => token(prec(1, seq("(**", /([^*]|\*+[^)*])*\*+/, ")"))),


        // Matches non-structural directives: #nowarn, #r, #load, #line, etc.
        // Structural directives (#if, #elif, #else, #endif) are handled by preproc_if
        // with dedicated high-priority tokens below.
        preproc_keyword: _ => token(seq("#", /[a-zA-Z_][a-zA-Z0-9_]*/, /[ \t]*/)),

        preproc_directive: $ => prec.right(seq(
            field('name', $.preproc_keyword),
            optional(field('argument', choice($.string_literal, $.int_literal, $.long_identifier))),
        )),

        // High-priority tokens for structural directives — prec(1) beats preproc_keyword (prec 0)
        // when both patterns match the same string. Longer match still wins unconditionally,
        // so "#ifdef" still falls to preproc_keyword (7 chars > 3/4).
        preproc_if_kw: _ => token(prec(1, seq("#if", /[ \t]*/))),
        preproc_elif_kw: _ => token(prec(1, seq("#elif", /[ \t]*/))),
        preproc_else_kw: _ => token(prec(1, seq("#else", /[ \t]*/))),
        preproc_endif_kw: _ => token(prec(1, seq("#endif", /[ \t]*/))),

        // Boolean condition expression used by #if and #elif.
        // && binds tighter than || (prec 2 vs 1).
        preproc_expression: $ => choice(
            $.identifier,
            "true",
            "false",
            seq("!", $.preproc_expression),
            seq("(", $.preproc_expression, ")"),
            prec.left(2, seq($.preproc_expression, "&&", $.preproc_expression)),
            prec.left(1, seq($.preproc_expression, "||", $.preproc_expression)),
        ),

        // #if COND
        //     body…
        // [#elif COND
        //     body…]
        // [#else
        //     body…]
        // #endif
        preproc_if: $ => seq(
            $.preproc_if_kw,
            field('condition', $.preproc_expression),
            repeat($._token),
            repeat($.preproc_elif_clause),
            optional($.preproc_else_clause),
            $.preproc_endif_kw,
        ),

        preproc_elif_clause: $ => seq(
            $.preproc_elif_kw,
            field('condition', $.preproc_expression),
            repeat($._token),
        ),

        preproc_else_clause: $ => seq(
            $.preproc_else_kw,
            repeat($._token),
        ),
    }
});
