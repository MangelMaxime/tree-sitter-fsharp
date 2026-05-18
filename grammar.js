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
    IF_EXPR:       18,
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

    extras: $ => [/\s+/, $.xml_doc_comment, $.line_comment, $.block_comment, $.block_doc_comment],

    // Without these declarations prec.dynamic is silently ignored.
    // record_type_field vs postfix_type: after `type_expression`, `long_identifier` could
    //   extend to postfix_type OR be the name of the next field.
    // record_field vs application_expression: after a value expression, a bare identifier
    //   could be a function argument OR the name of the next field.
    conflicts: $ => [
        [$.record_type_field, $.postfix_type],
        [$.record_field, $.application_expression],
    ],


    rules: {
        source_file: $ => repeat($._token),

        import_decl: ($) => seq("open", optional("type"), $.long_identifier),

        // namespace Foo.Bar  or  namespace global
        namespace_decl: $ => seq(
            "namespace",
            choice("global", field('name', $.long_identifier)),
        ),

        // module Foo.Bar                    (file-level / abbreviated module)
        // module [private|internal] Foo =   (explicit nested module header; body is top-level)
        module_decl: $ => seq(
            "module",
            optional($.access_modifier),
            optional("rec"),
            field('name', $.long_identifier),
            optional("="),
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
            optional(seq(
                "<",
                $.type_parameter,
                repeat(seq(",", $.type_parameter)),
                ">",
            )),
            optional($.primary_constructor),
            "=",
            // Body is optional: class/interface bodies use keywords (member, abstract, …) that
            // can't start a type_expression, so the parser naturally reduces with empty body and
            // the body tokens appear flat at the source_file level.
            optional(choice($.record_type_defn, $.union_type_defn, field('alias', $.type_expression))),
        )),

        // (x: int, y: string)  in  type Foo(x: int, y: string) =
        primary_constructor: $ => seq(
            "(",
            optional(seq(
                $.primary_ctor_param,
                repeat(seq(",", $.primary_ctor_param)),
            )),
            ")",
        ),

        primary_ctor_param: $ => seq(
            optional("?"),
            $.identifier,
            optional(seq(":", $.type_expression)),
        ),

        // member this.Name = expr
        // member this.Method arg : RetType = expr
        // static member Name args = expr
        // override this.ToString() = expr
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
        do_stmt: $ => seq("do", $._expression),

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
            optional(seq("of", field('fields', $.type_expression))),
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

        _expression: $ => prec(PREC.RARROW, choice(
            $.parenthesized_expression,
            $.application_expression,
            $.pipe_expression,
            $.or_expression,
            $.and_expression,
            $.additive_expression,
            $.multiplicative_expression,
            $.comparison_expression,
            $.unary_expression,
            $.cons_expression,
            $.infix_expression,
            $.list_expression,
            $.array_expression,
            $.record_expression,
            $.tuple_expression,
            $.int_literal,
            $.float_literal,
            $.char_literal,
            $.string_literal,
            $.verbatim_string,
            $.triple_quoted_string,
            $.bool_literal,
            $.unit,
            $.null_literal,
            $.long_identifier,
            $.if_expression,
            $.match_expression,
            $.lambda_expression,
            $.let_expression,
            $.computation_expression,
            $.for_expression,
            $.while_expression,
            $.set_expression,
            $.range_expression,
            $.upcast_expression,
            $.downcast_expression,
            $.type_test_expression,
            $.upcast_expr,
            $.downcast_expr,
            // CE result forms — also valid in if/match branches inside CEs
            $.ce_return_expr,
            $.ce_return_bang_expr,
            $.ce_yield_expr,
            $.ce_yield_bang_expr,
            $.ce_do_bang_expr,
        )),

        parenthesized_expression: $ => seq("(", $._expression, ")"),

        // Second argument is restricted to simple expressions (no let/if/match/lambda/operators)
        // so that `let x = 1\nlet y = 2` doesn't try to parse the second `let` as an argument.
        application_expression: $ => prec.left(PREC.APP_EXPR, seq(
            $._expression,
            $._simple_expression,
        )),

        _simple_expression: $ => choice(
            $.parenthesized_expression,
            $.list_expression,
            $.array_expression,
            $.record_expression,
            $.int_literal,
            $.float_literal,
            $.char_literal,
            $.string_literal,
            $.verbatim_string,
            $.triple_quoted_string,
            $.bool_literal,
            $.unit,
            $.null_literal,
            $.long_identifier,
        ),

        pipe_expression: $ => prec.left(PREC.PIPE_EXPR, seq(
            $._expression,
            choice("|>", "<|", ">>", "<<"),
            $._expression,
        )),

        or_expression: $ => prec.left(PREC.BOOL_OR, seq(
            $._expression, "||", $._expression,
        )),

        and_expression: $ => prec.left(PREC.BOOL_AND, seq(
            $._expression, "&&", $._expression,
        )),

        additive_expression: $ => prec.left(PREC.ADDITIVE, seq(
            $._expression,
            choice("+", "-"),
            $._expression,
        )),

        multiplicative_expression: $ => prec.left(PREC.MULTIPLICATIVE, seq(
            $._expression,
            choice("*", "/", "%"),
            $._expression,
        )),

        lambda_expression: $ => prec.right(PREC.FUN_EXPR,
            seq(
                "fun",
                repeat1($.parameter),
                "->",
                $._expression,
            ),
        ),

        comparison_expression: $ => prec.left(PREC.INFIX_OP, seq(
            $._expression,
            choice(">", "<", ">=", "<=", "=", "<>"),
            $._expression,
        )),

        unary_expression: $ => prec(PREC.PREFIX_EXPR, seq(
            choice("not", "~~~"),
            $._expression,
        )),

        cons_expression: $ => prec.right(PREC.INFIX_OP, seq(
            $._expression, "::", $._expression,
        )),

        infix_expression: $ => prec.left(PREC.INFIX_OP, seq(
            $._expression, $.symbolic_op, $._expression,
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
                "let",
                optional("rec"),
                optional(choice("inline", "mutable")),
                field('name', choice($.identifier, $.operator_name)),
                optional(seq("<", $.type_parameter, repeat(seq(",", $.type_parameter)), ">")),
                field('parameters', repeat($.parameter)),
                optional(seq(":", field('return_type', $.type_expression))),
                "=",
                $._expression,
            ),
        ),

        let_expression: ($) => prec.right(PREC.LET_EXPR,
            seq(
                "let",
                optional("rec"),
                optional(choice("inline", "mutable")),
                field('name', choice($.identifier, $.operator_name)),
                optional(seq("<", $.type_parameter, repeat(seq(",", $.type_parameter)), ">")),
                field('parameters', repeat($.parameter)),
                optional(seq(":", field('return_type', $.type_expression))),
                "=",
                $._expression,
                "in",
                $._expression,
            ),
        ),

        // x <- value   (mutable assignment)
        set_expression: $ => prec.right(PREC.LARROW,
            seq($._expression, "<-", $._expression),
        ),

        // 1..10  (simple range — stepped ranges like 1..2..10 nest as range(1, range(2,10)))
        // prec = 1 (lowest) so arithmetic/comparison binds first:
        //   0..n-1  →  0..(n-1)   ✓
        range_expression: $ => prec.right(1,
            seq($._expression, "..", $._expression),
        ),

        // ── Type casts ────────────────────────────────────────────────────────
        // expr :> Type   — upcast (widening, always safe)
        upcast_expression: $ => prec(PREC.TYPED_EXPR,
            seq($._expression, ":>", $.type_expression),
        ),

        // expr :?> Type  — downcast (narrowing, throws InvalidCastException on failure)
        downcast_expression: $ => prec(PREC.TYPED_EXPR,
            seq($._expression, ":?>", $.type_expression),
        ),

        // expr :? Type   — type test, returns bool
        type_test_expression: $ => prec(PREC.TYPED_EXPR,
            seq($._expression, ":?", $.type_expression),
        ),

        // upcast expr / downcast expr — keyword forms (type inferred by compiler)
        upcast_expr: $ => seq("upcast", $._expression),
        downcast_expr: $ => seq("downcast", $._expression),

        // ── For / While ───────────────────────────────────────────────────────

        // for x in xs do body   (imperative loop, returns unit)
        for_expression: $ => prec.right(PREC.IF_EXPR,
            seq("for", $.identifier, "in", $._expression, "do", $._expression),
        ),

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
            $.ce_do_bang_expr,
            $.use_binding,
            $.ce_use_bang_expr,
            $.ce_match_bang_expr,
            $.let_binding,
            $.do_stmt,
            $._expression,  // covers return/yield/return!/yield!/for/while/if/match/…
        ),

        // let! x = expr
        ce_let_bang_expr: $ => prec.right(PREC.LET_DECL,
            seq("let!", field('name', $.identifier), "=", $._expression),
        ),

        // do! expr
        ce_do_bang_expr: $ => seq("do!", $._expression),

        // use x = disposable  (also used as a top-level _token outside CEs)
        use_binding: $ => prec.right(PREC.LET_DECL,
            seq("use", optional("rec"), field('name', $.identifier), "=", $._expression),
        ),

        // use! x = expr
        ce_use_bang_expr: $ => prec.right(PREC.LET_DECL,
            seq("use!", field('name', $.identifier), "=", $._expression),
        ),

        // match! expr with | pat -> expr …
        ce_match_bang_expr: $ => prec.right(PREC.MATCH_EXPR,
            seq("match!", $._expression, "with", repeat1($.match_arm)),
        ),

        // return expr   (also in _expression so it works in if/match branches)
        ce_return_expr: $ => seq("return", $._expression),

        // return! expr
        ce_return_bang_expr: $ => seq("return!", $._expression),

        // yield expr
        ce_yield_expr: $ => seq("yield", $._expression),

        // yield! expr
        ce_yield_bang_expr: $ => seq("yield!", $._expression),

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
            repeat(seq("|", $.pattern)),
            optional(seq("when", $._expression)),
            "->",
            $._expression,
        ),

        pattern: $ => choice(
            $.wildcard_pattern,
            $.literal_pattern,
            $.identifier_pattern,
            $.cons_pattern,
            $.tuple_pattern,
            $.as_pattern,
            $.list_pattern,
            $.array_pattern,
            $.type_check_pattern,
        ),

        // | :? TypeName [as x] ->   (type-test pattern in match arms)
        type_check_pattern: $ => prec.right(seq(
            ":?",
            $.type_expression,
            optional(seq("as", $.identifier)),
        )),

        // x :: rest  or  x :: y :: rest  (right-associative)
        cons_pattern: $ => prec.right(1, seq($.pattern, "::", $.pattern)),

        wildcard_pattern: _ => "_",

        literal_pattern: $ => choice(
            $.int_literal,
            $.float_literal,
            $.char_literal,
            $.string_literal,
            $.bool_literal,
            $.unit,
            $.null_literal,
        ),

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
            seq("(", $.identifier, ":", $.type_expression, ")"),
            $.unit,  // () — unit parameter in methods like Foo()
        ),

        type_expression: $ => choice(
            $.function_type,
            $.tuple_type,
            $.postfix_type,
            $.generic_type,
            $.array_type,
            $.parenthesized_type,
            $.type_parameter,
            $.long_identifier,
        ),

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
            $.type_expression,
            repeat(seq(",", $.type_expression)),
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

        _token: $ => choice(
            $.attribute,
            $.namespace_decl,
            $.module_decl,
            $.import_decl,
            $.type_decl,
            $.let_binding,
            $.use_binding,
            $.member_defn,
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

        identifier: _ => /[a-zA-Z_][a-zA-Z0-9_']*/,

        long_identifier: $ =>
            prec.right(
                seq($.identifier, repeat(seq(".", $.identifier))),
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

        bool_literal: _ => choice("true", "false"),

        unit: _ => token(seq("(", ")")),

        null_literal: _ => token("null"),

        line_comment: _ => token(seq("//", choice(/[^/].*/, ""))),

        xml_doc_comment: _ => token(seq("///", /.*/)),

        block_comment: $ => seq(
            "(*",
            repeat(choice(/[^(*]/, /\*[^)]/, /\([^*]/, $.block_comment)),
            "*)",
        ),

        block_doc_comment: $ => seq(
            "(**",
            repeat(choice(/[^(*]/, /\*[^)]/, /\([^*]/, $.block_comment)),
            "*)",
        ),
    }
});
