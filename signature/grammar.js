/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

// F# signature files (.fsi): the declaration and type language of the main grammar
// with member signatures in place of bodies. Rules not reachable from `_token`
// are left out of the generated parser.

import fsharp from '../grammar.js';

export default grammar(fsharp, {
    name: 'fsharp_signature',

    supertypes: $ => [$._literal],

    // A leading `///` doc is either a standalone statement or the decoration of
    // the declaration that follows; GLR decides once the keyword is seen.
    conflicts: ($, original) => [
        ...original,
        [$.module_decl, $.type_decl, $.type_extension, $.val_field, $.exception_decl, $._token],
        [$.member_defn, $.abstract_member_defn, $.interface_impl, $.secondary_constructor, $.val_field, $._class_body_member],
        [$._class_body_member, $._secondary_ctor_core, $._member_defn_core, $._abstract_member_core, $._val_field_core],
        [$._class_body_member, $.secondary_constructor, $.member_defn, $.abstract_member_defn, $.interface_impl, $.val_field, $.record_type_defn],
        [$._module_decl_core, $._val_field_core, $._exception_decl_core, $._token],
    ],

    rules: {
        _token: $ => choice(
            $.preproc_directive,
            $.namespace_decl,
            $.module_decl,
            $.import_decl,
            $.type_decl,
            $.type_extension,
            $.exception_decl,
            $.val_field,
            $.attribute,
            prec.dynamic(-1, $.xml_doc_comment),
        ),

        _decl_or_comment: $ => $.attribute,

        _class_body_member: $ => choice(
            $.inherit_decl,
            $.member_defn,
            $.abstract_member_defn,
            $.interface_impl,
            $.secondary_constructor,
            $.val_field,
            $.attribute,
            prec.dynamic(-1, $.xml_doc_comment),
        ),

        // `member Name: T [with get, set]`, also `static`, `default`, `override`.
        _member_defn_core: $ => prec.dynamic(1, seq(
            repeat($.attribute),
            choice(
                seq(optional("static"), "member"),
                "default",
                "override",
            ),
            optional("inline"),
            optional($.access_modifier),
            field('name', choice($.identifier, $.operator_name, $.active_pattern_name)),
            $._abstract_tail,
            optional($._when_constraints),
            optional(seq("=", $._sig_constant)),
        )),

        // `[<Literal>] val X: int = 3` and `member X: int = 3` carry a constant.
        _sig_constant: $ => choice($._literal, $.negative_literal, $.long_identifier),

        // Verbose `type T = ... with ... end`.
        _type_augmentation: $ => prec.right(seq(
            "with",
            optional(choice($._class_body_block, $._class_body_member)),
            optional("end"),
        )),

        // `value: 'T | null`: `| null` is one token, so it never reads as a union case.
        labelled_type: $ => seq(
            choice(seq($._label_attr, repeat1($.attribute)), $._label_gate),
            optional("?"),
            field('name', $.identifier),
            ":",
            field('type', choice(
                $.nullable_type,
                $.struct_tuple_type,
                $._atomic_type,
                $.anonymous_record_type,
                $.struct_anonymous_record_type,
                $.flexible_type,
            )),
        ),

        // `ImmutableArray<'T>.Builder`: a nested type of a generic instantiation.
        generic_type: $ => prec(4, seq(
            $.long_identifier,
            "<",
            $._generic_type_arg,
            repeat(seq(",", $._generic_type_arg)),
            ">",
            optional(seq(".", $.long_identifier)),
        )),

        inherit_decl: $ => seq("inherit", field('base', $.type_expression)),

        _secondary_ctor_core: $ => seq(
            repeat($.attribute),
            optional($.access_modifier),
            "new",
            ":",
            $.type_expression,
        ),

        _val_tail: $ => seq(
            optional($.type_parameter_list),
            ":",
            $.type_expression,
            optional($._when_constraints),
            optional(seq("=", $._sig_constant)),
        ),

        // `module M =` followed by a verbose `begin ... end` block on the next line.
        _module_rhs: $ => seq("=", optional(choice(
            field('abbrev', $.long_identifier),
            seq("begin", repeat($._token), "end"),
            seq($._block_open, repeat($._token), $._layout_end),
            seq($._block_open, "begin", repeat($._token), "end", $._layout_end),
        ))),

        // `( |A|B| )` with spaces inside the parens (ProvidedTypes.fsi).
        active_pattern_name: _ => token(seq(
            "(",
            /[ \t]*/,
            "|",
            choice(/[\p{L}_][\p{L}\p{Nd}_']*/, /``[^`\n\r\t]+``/),
            repeat(seq("|", choice(/[\p{L}_][\p{L}\p{Nd}_']*/, /``[^`\n\r\t]+``/))),
            optional(seq(/[ \t]*/, "|", /[ \t]*/, "_", /[ \t]*/)),
            "|",
            /[ \t]*/,
            ")",
        )),

        _enum_case_bare: $ => seq(
            field('name', $.identifier),
            "=",
            field('value', choice($.int_literal, $.negative_literal, $.char_literal)),
        ),

        _literal: $ => choice(
            $.measure_literal,
            $.int_literal,
            $.float_literal,
            $.char_literal,
            $.string_literal,
            $.verbatim_string,
            $.triple_quoted_string,
            $.bool_literal,
            $.unit,
            $.null_literal,
        ),

        _attribute_arg: $ => choice(
            $.int_literal, $.float_literal, $.char_literal,
            $.string_literal, $.verbatim_string, $.triple_quoted_string,
            $.bool_literal, $.long_identifier,
        ),

        _attr_paren_arg: $ => choice(
            seq(field('name', $.identifier), "=", $._attr_value),
            alias($.attr_ascribed_arg, $.type_ascription_expression),
            $._attr_value,
        ),

        attr_ascribed_arg: $ => seq($._attr_value, ":", $.type_expression),

        _attr_value: $ => choice(
            $._literal,
            $.negative_literal,
            $.long_identifier,
            $.type_keyword_expression,
            seq("enum", "<", $.type_expression, ">", choice($.int_literal, seq("(", $.int_literal, ")"))),
            prec.left(1, seq($._attr_value, choice("|||", "+", "&&&"), $._attr_value)),
            seq("[|", optional(seq($._attr_value, repeat(seq(";", $._attr_value)))), "|]"),
        ),
    },
});
