/**
 * @file F# tree sitter definition focused on Helix
 * @author Mangel Maxime
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const PREC = {
    SEQ_EXPR:      1,
    THEN_EXPR:     2,
    RARROW:        3,
    INFIX_OP:      4,
    LET_DECL:      7,
    DO_EXPR:       8,
    FUN_EXPR:      8,
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
    TUPLE_EXPR:    16,
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
    LET_EXPR:      60,
};

export default grammar({
    name: "fsharp",

    word: $ => $.identifier,

    extras: $ => [/\s+/, $.xml_doc_comment, $.line_comment, $.block_comment, $.block_doc_comment],


    rules: {
        source_file: $ => repeat($._token),

        import_decl: ($) => seq("open", optional("type"), $.long_identifier),

        _expression: $ => prec(PREC.RARROW, choice(
            $.comparison_expression,
            $.int_literal,
            $.float_literal,
            $.char_literal,
            $.string_literal,
            $.verbatim_string,
            $.triple_quoted_string,
            $.bool_literal,
            $.unit,
            $.null_literal,
            $.identifier,
            $.long_identifier,
            $.if_expression,
            $.match_expression,
            $.lambda_expression,
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

        let_binding: ($) => prec.right(PREC.LET_DECL,
            seq(
                "let",
                optional("rec"),
                field('name', $.identifier),
                field('parameters', repeat($.parameter)),
                optional(seq(":", field('return_type', $.type_expression))),
                "=",
                $._expression,
            ),
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
            repeat(seq("|", $.pattern)),
            "->",
            $._expression,
        ),

        pattern: $ => choice(
            $.wildcard_pattern,
            $.literal_pattern,
            $.identifier_pattern,
            $.tuple_pattern,
        ),

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
            seq($.long_identifier, $.pattern),
        ),

        tuple_pattern: $ => seq(
            "(",
            $.pattern,
            repeat(seq(",", $.pattern)),
            ")",
        ),

        parameter: $ => choice(
            $.identifier,
            seq("(", $.identifier, ":", $.type_expression, ")"),
        ),

        type_expression: $ => prec(2, choice(
            $.identifier,
            $.long_identifier,
        )),

        _token: $ => choice(
            $.import_decl,
            $.let_binding,
            $.lambda_expression,
            $.if_expression,
            $.match_expression,
            $.char_literal,
            $.string_literal,
            $.verbatim_string,
            $.triple_quoted_string,
            $.bool_literal,
            $.unit,
            $.null_literal,
            $.long_identifier,
            $.xml_doc_comment,
            $.line_comment,
            $.block_comment,
            $.block_doc_comment,
            $.int_literal,
            $.float_literal,
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
                token(seq(/[0-9][0-9_]*/, ".", /[0-9_]*/, optional(seq(/[eE]/, optional(/[+-]/), /[0-9]+/)))),
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
