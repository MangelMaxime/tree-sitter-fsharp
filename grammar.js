/**
 * @file F# tree sitter definition focused on Helix
 * @author Mangel Maxime
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

export default grammar({
    name: "fsharp",

    word: $ => $.identifier,

    extras: $ => [/\s+/, $.xml_doc_comment, $.line_comment, $.block_comment, $.block_doc_comment],

    conflicts: $ => [
        [$.identifier_pattern, $.long_identifier],
        [$.pattern, $.long_identifier],
    ],

    rules: {
        source_file: $ => repeat($._token),

        import_decl: ($) => seq("open", optional("type"), $.long_identifier),

        _expression: $ => prec(3, choice(
            $.comparison_expression,
            $.int_literal,
            $.float_literal,
            $.bool_literal,
            $.unit,
            $.null_literal,
            $.identifier,
            $.long_identifier,
            $.if_expression,
            $.match_expression,
        )),

        comparison_expression: $ => prec.left(4, seq(
            $._expression,
            choice(">", "<", ">=", "<=", "=", "<>"),
            $._expression,
        )),

        if_expression: $ => prec.right(2,
            seq(
                "if",
                $._expression,
                "then",
                $._expression,
                repeat(seq("elif", $._expression, "then", $._expression)),
                optional(seq("else", $._expression)),
            ),
        ),

        let_binding: ($) => prec.right(1,
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

        match_expression: ($) => prec.right(2,
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
            $.bool_literal,
            $.unit,
            $.null_literal,
        ),

        identifier_pattern: $ => choice(
            $.identifier,
            seq($.identifier, $.pattern),
            $.long_identifier,
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
            $.if_expression,
            $.match_expression,
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
