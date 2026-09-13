/**
 * @file F# tree sitter definition focused on Helix
 * @author Mangel Maxime
 * @license Apache 2.0
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

// Type-level precedences: a strict total order, each tier wraps the next.
const TYPE_PREC = {
    FUNCTION: 1,   // int -> string (right-assoc)
    TUPLE:    2,   // int * string
    POSTFIX:  3,   // int list, int option (left-assoc)
    APP:      4,   // list<int>, int[]
};

// Expression-level precedences; equal values are deliberate.
const PREC = {
    // Tier 1 - lowest binding: bodies consume the whole trailing expression chain.
    SEQ_EXPR:       1,   // virtual-semi sequence (must be loosest)
    PIPE_EXPR:      1,   // |>, <|, >>, <<
    FUN_EXPR:       1,   // `fun x -> ...`
    LET_EXPR:       1,   // `let x = ... in ...`
    TUPLE_EXPR:     1,   // `,` binds looser than `||`

    BOOL_OR:        2,   // ||
    BOOL_AND:       3,   // &&
    INFIX_OP:       4,   // = <> < > <= >= :: and custom symbolic
    ADDITIVE:       5,   // + -
    MULTIPLICATIVE: 6,   // * / %

    // Tier 7+ - declaration / control flow / application.
    LET_DECL:       7,   // `let f x = ...`
    MATCH_EXPR:     8,   // match / try-with / function
    IF_EXPR:        14,  // if / elif / else / for / while
    PREFIX_EXPR:    15,  // unary `not` / `~~~` / `-` / `&` / `lazy` / `assert`
    CE_EXPR:        15,  // `builder { ... }`; below APP_EXPR so `f { ... }` is application
    APP_EXPR:       16,  // `f x`
    LARROW:         16,  // `expr <- expr`

    // Tier 19+ - atomic / postfix access.
    DOT:            19,  // `a.b.c` long_identifier chain
    INDEX_EXPR:     20,  // `arr.[i]`
    PAREN_EXPR:     21,  // `(expr)` / `begin ... end` / quotations
    TYPED_EXPR:     22,  // `(expr : ty)`
    DOTDOT_SLICE:   23,  // `expr..` and `..expr` inside index args
    NEW_OBJ:        24,  // `new T(...)`
};

// `xml_doc_comment` is also an extra: where this slot is not valid the doc is trivia.
function decoration($) {
    return seq(repeat($.xml_doc_comment), repeat($.attribute));
}

// Multi-line form: `_record_open` (scanner captures the field column), `_bracket_semi`
// separators, `_bracket_close`. Single-line form: explicit `;` only.
function indentedOrInlineFieldList($, field, sepPrec, opts) {
    return choice(
        // `_record_open` peeks `ident =`/`ident :` and is suppressed for `{ new ...}` and
        // `{ base with ...}`, so the object-expression and copy-update branches match instead.
        seq(
            $._record_open,
            field,
            // No prec.dynamic here: a prec above the field's application would end the
            // value at its head (`X = abs 3` read as `X = abs`).
            repeat(seq(choice(";", $._bracket_semi), field)),
            optional(choice(";", $._bracket_semi)),
            $._bracket_close,
        ),
        // Also used where `_record_open` does not fire (a copy-update's field list).
        seq(
            field,
            repeat(prec.dynamic(sepPrec, seq(";", field))),
            optional(";"),
        ),
    );
}

// Keywords that are never a bare identifier. Contextual words that are legal
// identifiers (`not`, `base`, `global`, `fixed`, `void`, the query operators) stay out.
const GLOBAL_RESERVED = [
    'abstract', 'delegate', 'downcast', 'downto',
    'finally', 'inherit', 'try', 'upcast',
    'type',
    'assert', 'default', 'exception', 'function', 'inline', 'interface',
    'internal', 'module', 'mutable', 'namespace', 'new', 'or',
    'public', 'rec', 'static', 'to', 'val', 'when',
    'then', 'elif', 'else', 'if',
    // Deliberately not reserved: fun open override while class end of private member
    // (each costs valid files), lazy (`lazy<int>` is a type name), extern, begin (verbose syntax).
];

export default grammar({
    name: "fsharp",

    word: $ => $.identifier,

    // Supertypes are queryable categories in node-types.json; the tree shape is unchanged.
    supertypes: $ => [
        $._expression,
        $._simple_expression,
        $._literal,
    ],

    // `query_ce` is active only inside `computation_expression` bodies, so query
    // operators like `where`/`select` stay plain identifiers elsewhere (`List.where`).
    reserved: {
        global: _ => GLOBAL_RESERVED,
        // `reserved(name, rule)` replaces the active set, so the global words must be re-listed.
        query_ce: _ => [...GLOBAL_RESERVED,
            'groupBy', 'groupValBy', 'groupJoin',
            'join', 'leftOuterJoin', 'on', 'into',
        ],
    },

    // Emitted by src/scanner.c.
    externals: $ => [
        $._error_sentinel,    // unused in rules; valid only during error recovery
        $._layout_open,       // generic body open (Decl/Then/Do/Let) after =/then/else/->/do
        $._layout_semi,       // generic separator (next line == body col)
        $._layout_end,        // generic close (next line < body col)
        $._match_open,        // arm-list open after with/function/(lambda)->
        $._match_end,         // arm-list close (dedent below arm col, or == col & not `|`)
        $._bracket_open,      // [ / [| / { block body on its own line(s)
        $._bracket_semi,      // newline-aligned element/field separator
        $._bracket_close,     // ] / |] / } closing a block bracket
        $._record_open,       // `{` record body - peeks `ident =`/`ident :`; not new/copy-update
        $._block_open,        // newline-gated layout open for MODULE bodies (closes via _layout_end)
        $._type_open,         // newline-gated layout open for TYPE bodies (also closes before `with`)
        $._expr_open,         // expression body (then/elif body, lambda, let-in value); closes before else/elif/in
        $._else_open,         // final-else body; suppressed when next token is `if` (-> flat else-if)
        $._float_trailing_dot,
        // Interpolated-string text chunks are external so a chunk starting with `//`
        // is not lexed as a `line_comment` extra.
        $._interp_string_text,
        $._interp_verbatim_text,
        $._interp_triple_text,
        $._for_open,          // `for ... do` body open (suppressed before query-CE operators)
        $._ctor_attr,         // zero-width gate: `[<...>]+ (` ahead (attribute on a primary ctor)
        $._try_open,          // try/finally body open - closes before `with`/`finally`
        $._label_attr,        // zero-width gate: `[<...>]+ ident:` ahead (attribute on a labelled param)
        $._element_dsl_open,  // zero-width gate: `ident ( ... ) {` ahead (element-DSL builder)
        $._and_docs_open,     // zero-width gate: `///` docs followed by `and`
        $._case_docs_open,    // zero-width gate: `///` docs followed by a `|` case
        $._paren_field_open,  // `Foo(ident = ...)` named-field body open - a bracket context for newline-aligned fields
        $._ce_brace_open,     // the `{` of a computation_expression body; emitted (consuming `{`) only when the content is a CE body, not record/object/copy-update
        $.block_comment,      // `(* ... *)` with nesting; `(*)` stays the multiply operator
        $.block_doc_comment,  // `(** ... *)` - same scan, classified by the 3rd char
        $._then_open,         // then/elif body open - only these bodies close at a mid-line `else`
        $._lazy_open,         // lazy block-body open - declines inline bodies (`lazy x`)
        $._ctor_tuple_gate,   // zero-width gate: `ident ( ... ) ,` ahead (`let Ctor(a, b), rest = ...`)
        $._preproc_break,     // zero-width: a `#if`-family directive line separates two declarations
        $._decl_semi,         // a statement-ending `;` whose next line starts a declaration (consumed as an extra)
        $._members_open,      // zero-width: members indented below a same-line type body (`type DU = | A`)
        $._label_gate,        // zero-width: `ident :` ahead (not `::` `:>` `:?` `:=`)
        $._paren_block_open,  // zero-width: `(` followed by a newline - the body is a layout block closed by `)`
        $._infix_block_open,  // zero-width: `&&` / `||` ending a line with a deeper next line - the right operand is a layout block
        $._field_block_open,  // zero-width: record field `=` followed by a newline - the value is a layout block
    ],

    extras: $ => [/\s+/, $.xml_doc_comment, $.line_comment, $.block_comment, $.block_doc_comment,
        $.line_directive,
        // `;;` (FSI terminator) is skippable anywhere; longer match beats the `;` separator.
        $.fsi_terminator,
        // A `;` terminating a statement before a declaration keyword; emitted by the
        // scanner so `sequence_expression` never sees it (LR(1) cannot see past the `;`).
        $._decl_semi,
        // Both branches of `#if/#else` parse as real code; the directive lines are
        // trivia and the scanner skips them so they never close layout.
        $.preproc_if, $.preproc_elif, $.preproc_else_kw, $.preproc_endif_kw],

    // GLR forks; without a conflict entry prec.dynamic is silently ignored.
    conflicts: $ => [
        // `(f, Ctor(h, t): T)`: a constructor application with or without a following type.
        [$.tuple_typed_pattern, $._tuple_elem_pattern],
        // `| A of x: int`: a labelled type in the anonymous field slot vs the named-field list.
        [$.union_case_named_fields, $.type_expression],
        // `expr ;`: the `;` continues a sequence_expression or terminates the statement.
        [$.sequence_expression, $._token],
        // A leading `///` doc: decoration of the following declaration vs a standalone doc.
        [$.module_decl, $.type_decl, $.type_extension, $.let_binding, $.exception_decl, $.val_field, $._token],
        // The same fork inside a class body.
        [$._class_body_member, $.secondary_constructor, $.member_defn, $.abstract_member_defn, $.interface_impl, $.val_field, $.let_binding],
        [$._class_body_member, $.secondary_constructor, $.member_defn, $.abstract_member_defn, $.interface_impl, $.val_field, $.record_type_defn, $.let_binding],
        // Keep despite the generator's "unnecessary conflict" warning: without it
        // `type X = [<attr>] member ...` is an unresolved conflict (build error).
        [$._decl_or_comment, $._secondary_ctor_core, $._member_defn_core, $._abstract_member_core, $._val_field_core, $._let_binding_core],
        // After a field value, a bare identifier extends it or names the next record field.
        [$._record_field_core, $.postfix_type],
        // After `name: T` in a named union field, `*` starts the next field or extends T into a tuple.
        [$._union_field_type, $.type_expression],
        // A lone long_identifier is a measure or a type expression.
        [$.measure_expression, $.type_expression],
        [$.measure_expression, $._atomic_type],
        // After `type Foo`, `=` chooses type_decl and `with` chooses type_extension.
        [$._type_head, $.type_extension_name],
        // After `module M =`, an identifier is an abbreviation target or the first nested declaration.
        [$._module_rhs],
        // `[<...>]` / `///` at top level: a standalone `_decl_or_comment` or a decl's decoration prefix.
        [$._decl_or_comment, $._let_binding_core, $._module_decl_core, $._exception_decl_core, $._val_field_core],
        // The same fork inside a class body.
        [$._decl_or_comment, $.let_binding, $.member_defn, $.abstract_member_defn, $.secondary_constructor, $.val_field],
        // `expr <`: a type_application_expression (`Map.empty<string, int>`) or a `<` comparison.
        [$.type_application_expression, $._expression],
        [$.type_application_expression, $._simple_expression],
        // `( x` in a pattern: tuple_pattern or tuple_typed_first_pattern (`x: type` first).
        [$.pattern, $.tuple_typed_pattern],
        [$.identifier_pattern, $.tuple_typed_pattern],
        // `name: T` after `_label_gate`: labelled_type, or `name` as a plain type (`x: int list`).
        [$.labelled_type, $.type_expression],
        // `#Foo<int>`: `<` extends the flexible type into a generic_type or starts a comparison.
        [$.flexible_type, $.generic_type],
        // `( _ ...` in a param list: a lambda pattern element or a tuple_param wildcard.
        [$.tuple_param, $.pattern],
        [$.tuple_param, $.destructure_parameter],
    ],


    rules: {
        source_file: $ => repeat($._token),

        fsi_terminator: _ => token(";;"),

        import_decl: ($) => seq("open", optional("type"), $.long_identifier),

        namespace_decl: $ => seq(
            "namespace",
            optional("rec"),
            choice("global", field('name', $.long_identifier)),
        ),

        module_decl: $ => choice(
            seq(repeat1($.xml_doc_comment), field('decl', alias($._module_decl_core, $.module_decl))),
            $._module_decl_core,
        ),

        _module_decl_core: $ => seq(
            repeat($.attribute),
            "module",
            // Attributes may also sit between `module` and the name.
            repeat($.attribute),
            optional($.access_modifier),
            optional("rec"),
            field('name', $.long_identifier),
            optional($._module_rhs),
        ),

        access_modifier: _ => choice("private", "internal", "public"),

        attribute: $ => seq(
            "[<",
            $.attribute_target,
            repeat(seq(";", $.attribute_target)),
            optional(";"),
            ">]",
        ),

        attribute_target: $ => seq(
            // Target specifier `[<return: Struct>]`, `[<assembly: ...>]`; `return`/`module`/`type` are keywords.
            optional(seq(
                field('target', choice($.identifier, "return", "module", "type")),
                ":",
            )),
            field('name', $.long_identifier),
            optional(choice(
                seq(
                    "(",
                    optional(seq($._attr_paren_arg, repeat(seq(",", $._attr_paren_arg)))),
                    ")",
                ),
                // Bare single-argument form: `[<Foo "x">]`.
                field('argument', $._attribute_arg),
            )),
        ),

        // `DefaultParameterValue(null: string | null)`: an ascribed argument (F# 9 nullness).
        _attr_paren_arg: $ => choice(
            $._expression,
            alias($.attr_ascribed_arg, $.type_ascription_expression),
        ),
        attr_ascribed_arg: $ => seq($._expression, ":", $.type_expression),

        // Only forms that neither start with `(` nor contain `>` (`>]` closes the attribute).
        _attribute_arg: $ => choice(
            $.int_literal, $.float_literal, $.char_literal,
            $.string_literal, $.verbatim_string, $.triple_quoted_string,
            $.interpolated_string, $.interpolated_verbatim_string,
            $.interpolated_triple_string, $.bool_literal, $.long_identifier,
            $.array_expression, $.list_expression,
        ),


        _type_decl_body: $ => choice(
            $.record_type_defn,
            $.union_type_defn,
            $.enum_type_defn,
            $.delegate_type_defn,
            $.struct_type_defn,
            $.class_type_defn,
            $.interface_type_defn,
            field('alias', $.measure_expression),
            // Reciprocal measure: `[<Measure>] type hertz = / second`.
            field('alias', alias(seq("/", $.measure_expression), $.measure_expression)),
            // Inline IL type definition: `type ``[,]``<'T> = (# "!0[0 ...,0 ...]" #)`.
            field('alias', $.inline_il_expression),
            prec.dynamic(1, field('alias', $.type_expression)),
        ),


        // `=` is optional: `[<Measure>] type kg` and empty class/interface bodies have none.
        type_decl: $ => choice(
            seq(repeat1($.xml_doc_comment), field('decl', alias($._type_decl_core, $.type_decl))),
            $._type_decl_core,
        ),

        _type_decl_core: $ => prec.right(seq($._type_head, optional($._type_rhs), repeat($.type_and_decl))),

        _type_head: $ => prec.right(seq(
            "type",
            repeat($.attribute),
            // Visibility of the type; the modifier before the primary constructor is the constructor's.
            optional($.access_modifier),
            // ML-style prefix type parameters: `type 'T set = ...`, `type ('a, 'b) pair = ...`.
            optional($.prefix_type_parameters),
            field('name', $.identifier),
            optional($.type_parameter_list),
            // `type Foo<'T> when 'T: comparison = ...`: constraints may sit outside the `<...>` list.
            optional($._when_constraints),
            // Known gap: docs between the name and the primary constructor do not attach.
            optional(prec(20, seq(
                optional($.access_modifier),
                $.primary_constructor,
            ))),
            optional(seq("as", field('self', $.identifier))),
        )),

        // Without `=`, a `with` belongs to `type_extension`.
        _type_rhs: $ => prec.right(seq(
            "=",
            optional($._type_decl_body_or_class),
            optional($._type_augmentation),
        )),

        // `_and_docs_open` is emitted only when doc lines are followed by `and`; ungated,
        // a doc between two ordinary declarations commits to a doomed and-clause.
        _and_docs: $ => seq($._and_docs_open, repeat1($.xml_doc_comment)),

        type_and_decl: $ => prec.right(choice(
            seq($._and_docs, field('decl', alias($._type_and_core, $.type_and_decl))),
            $._type_and_core,
        )),

        _type_and_core: $ => prec.right(seq($._type_and_head, optional($._type_rhs))),

        _type_and_head: $ => prec.right(seq(
            "and",
            repeat($.attribute),
            optional($.access_modifier),
            optional($.prefix_type_parameters),
            field('name', $.identifier),
            optional($.type_parameter_list),
            optional($._when_constraints),
            optional($.access_modifier),
            optional($.primary_constructor),
            optional(seq("as", field('self', $.identifier))),
        )),

        // `type Point = { ... } with member ...`: the members may be indented or on the `with` line.
        _type_augmentation: $ => prec.right(seq(
            "with",
            optional(choice(
                $._class_body_block,
                $._class_body_member,
            )),
        )),

        // `type Foo with ...` (no `=`): intrinsic or optional extension.
        type_extension: $ => choice(
            seq(repeat1($.xml_doc_comment), field('decl', alias($._type_extension_core, $.type_extension))),
            $._type_extension_core,
        ),

        _type_extension_core: $ => seq(
            "type",
            repeat($.attribute),
            optional($.access_modifier),
            field('name', $.type_extension_name),
            optional($.type_parameter_list),
            "with",
            optional($._class_body_block),
        ),

        _type_decl_body_or_class: $ => choice(
            // `_members_open` is emitted only when the next line indents past the enclosing
            // context and starts with a member keyword or `[<`.
            seq($._type_decl_body, optional(seq($._members_open, repeat1($._class_body_member), $._layout_end))),
            seq(
                $._type_open,
                choice(
                    // Members may follow the body in the same indented block, with or without `with`.
                    // A `with` on its own line at the type column goes through `_type_augmentation` instead.
                    seq($._type_decl_body, optional("with"), repeat($._class_body_member)),
                    repeat1($._class_body_member),
                ),
                $._layout_end,
            ),
            // A single member on the `=` line: no layout open fires.
            $._class_body_member,
        ),

        // No comment forms here: a comment as a regular token ends the enclosing rule early.
        _decl_or_comment: $ => choice(
            prec.dynamic(-1, $.attribute),
            $.let_binding,
            $.do_stmt,
        ),

        _class_body_member: $ => choice(
            $.inherit_decl,
            $.member_defn,
            $.abstract_member_defn,
            $.interface_impl,
            $.secondary_constructor,
            $.val_field,
            $._decl_or_comment,
            // Un-attachable trailing `///` docs become their own member.
            prec.dynamic(-1, $.xml_doc_comment),
        ),

        type_extension_name: $ => choice(
            seq($.identifier, repeat1(seq(".", $.identifier))),
            $.identifier,
        ),

        tuple_params: $ => seq(
            "(",
            optional(seq(
                $.tuple_param,
                // Redundant parens only after a comma: a parenthesised first element
                // collides with the curried `(ident: type)` parameter form.
                repeat(seq(",", choice($.tuple_param, seq("(", $.tuple_param, ")")))),
            )),
            ")",
        ),

        // `_ctor_attr` is emitted only when `[<...>]+` is immediately followed by `(`,
        // so a standalone attribute on the next declaration is not grabbed.
        primary_constructor: $ => prec(20, choice(
            // `()` lexes as the unit token, so an empty attributed ctor needs its own branch.
            seq(
                optional(seq($._ctor_attr, repeat($.xml_doc_comment), repeat($.attribute), optional($.access_modifier))),
                $.unit,
            ),
            seq(
                // The scanner requires at least one doc or attribute row before the `(`.
                optional(seq($._ctor_attr, repeat($.xml_doc_comment), repeat($.attribute), optional($.access_modifier))),
                "(",
                $.tuple_param,
                repeat(seq(",", $.tuple_param)),
                ")",
            ),
        )),

        tuple_param: $ => seq(
            repeat($.attribute),
            optional("?"),
            choice(
                $.identifier,
                $.wildcard_pattern,
                // `(AesKey key: T, iv)`: a union-case deconstruction as a tuple-param element.
                prec.right(seq($.long_identifier, repeat1($._tuple_elem_pattern))),
                $.tuple_pattern,
                $.record_pattern,
            ),
            // A `when` after a param's type can only be a constraint clause.
            optional(seq(":", $.type_expression,
                optional(choice($._when_constraints, seq(":>", $.type_expression))))),
        ),

        secondary_constructor: $ => choice(
            seq(repeat1($.xml_doc_comment), field('decl', alias($._secondary_ctor_core, $.secondary_constructor))),
            $._secondary_ctor_core,
        ),

        _secondary_ctor_core: $ => prec.right(prec.dynamic(1, choice(
            seq(
                repeat($.attribute),
                optional($.access_modifier),
                "new",
                field('parameters', choice($.tuple_params, $.identifier)),
                optional(seq("as", field('self', $.identifier))),
                $._ctor_rhs,
            ),
            // Signature form: `new: unit -> T`.
            seq(
                repeat($.attribute),
                optional($.access_modifier),
                "new",
                ":",
                $.type_expression,
            ),
        ))),

        // A trailing `when ...` constraint clause is legal after the return type (inline SRTP).
        _return_type_annot: $ => seq(
            ":",
            repeat($.attribute),    // `let f(x) : [<A>] int = ...`
            field('return_type', $.type_expression),
            optional(seq(
                "when",
                $.type_constraint,
                repeat(seq("and", $.type_constraint)),
            )),
        ),

        _instance_member_prefix: $ => seq(
            choice("member", "override", "default"),
            optional("inline"),
            optional($.access_modifier),
            field('self', $.member_self_ident),
            ".",
            field('name', choice($.identifier, $.operator_name, $.active_pattern_name)),
            optional($.type_parameter_list),
        ),

        _static_member_prefix: $ => seq(
            "static",
            "member",
            optional("inline"),
            optional($.access_modifier),
            field('name', choice($.identifier, $.operator_name, $.active_pattern_name)),
            optional($.type_parameter_list),
        ),

        // The body is optional so a mid-edit `member this.Foo() =` still yields a `member_defn`
        // node for Helix's indent `@extend` to anchor on.
        _method_body: $ => prec.right(seq(
            field('parameters', repeat($.parameter)),
            optional($._return_type_annot),
            $._method_rhs,
        )),

        _method_rhs: $ => prec.right(choice(
                seq(
                    "=",
                    optional(choice(
                        $._layout_body,
                        // `#if`/`#else` branches each ending in `=`: the body belongs to the last branch.
                        $._preproc_break,
                    )),
                ),
                // `#if`/`#else` branches of one member head: the first branch has no `=` of its own.
                $._preproc_break,
        )),

        _accessor_body: $ => prec.right(seq(
            "with",
            $.property_accessor,
            optional(seq("and", $.property_accessor)),
        )),

        // prec.dynamic(1) on every branch attaches leading `[<...>]`/`///` to the member
        // instead of leaving them as standalone `_class_body_member` siblings.
        member_defn: $ => choice(
            seq(repeat1($.xml_doc_comment), field('decl', alias($._member_defn_core, $.member_defn))),
            $._member_defn_core,
        ),

        _member_defn_core: $ => choice(
            prec.dynamic(1, seq(
                repeat($.attribute),
                $._instance_member_prefix, $._method_body,
            )),
            prec.dynamic(1, seq(
                repeat($.attribute),
                $._static_member_prefix, $._method_body,
            )),
            prec.dynamic(1, seq(
                repeat($.attribute),
                $._instance_member_prefix, $._accessor_body,
            )),
            prec.dynamic(1, seq(
                repeat($.attribute),
                $._static_member_prefix, $._accessor_body,
            )),
            // `override val` / `default val` implement an abstract auto-property.
            prec.dynamic(1, prec.right(seq(
                repeat($.attribute),
                choice(
                    seq(optional("static"), "member"),
                    "override",
                    "default",
                ),
                "val",
                optional($.access_modifier),
                field('name', $.identifier),
                $._member_val_rhs,
            ))),
        ),



        property_accessor: $ => seq(
            optional("inline"),
            choice("get", "set"),
            field('parameters', repeat($.parameter)),
            $._accessor_rhs,
        ),

        auto_property_accessors: _ => seq(
            "with",
            choice("get", "set"),
            optional(seq(",", choice("get", "set"))),
        ),

        member_self_ident: $ => $.identifier,

        abstract_member_defn: $ => choice(
            seq(repeat1($.xml_doc_comment), field('decl', alias($._abstract_member_core, $.abstract_member_defn))),
            $._abstract_member_core,
        ),

        _abstract_member_core: $ => prec.dynamic(1, prec.right(seq(
            repeat($.attribute),
            optional("static"),
            "abstract",
            optional("member"),
            field('name', $.identifier),
            $._abstract_tail,
        ))),

        inherit_decl: $ => prec.right(seq(
            "inherit",
            field('base', $.type_expression),
            optional($._paren_args),
            optional(seq("as", field('alias', $.identifier))),
        )),

        interface_impl: $ => choice(
            seq(repeat1($.xml_doc_comment), field('decl', alias($._interface_impl_core, $.interface_impl))),
            $._interface_impl_core,
        ),

        _interface_impl_core: $ => prec.right(seq(
            "interface",
            field('type', $.type_expression),
            optional(seq(
                "with",
                optional($._with_members),
            )),
        )),

        do_stmt: $ => prec(3, seq(optional("static"), "do", $._indented_or_inline_body)),

        val_field: $ => choice(
            seq(repeat1($.xml_doc_comment), field('decl', alias($._val_field_core, $.val_field))),
            $._val_field_core,
        ),

        // Also the signature form `val f: int -> int` and the initialised `[<Literal>] val X: int = 3`.
        _val_field_core: $ => seq(
            repeat($.attribute),
            optional("static"),
            "val",
            optional("inline"),
            optional("mutable"),
            optional($.access_modifier),
            field('name', choice($.identifier, $.operator_name, $.active_pattern_name)),
            $._val_tail,
        ),

        // The first case may omit its `|` (`type X = A | B`); a bare first case is kept
        // attribute-less so it cannot be confused with an attributed type member.
        union_type_defn: $ => prec.right(seq(
            optional($.access_modifier),
            choice(
                // `#nowarn`/`#warnon` directives may sit between cases.
                repeat1(choice($.union_case, $.preproc_directive)),
                // A lone bare name (`type X = A`) is an abbreviation, so a `|`-case must follow.
                seq(
                    alias($.union_case_bare, $.union_case),
                    repeat1($.union_case),
                ),
                // `A of B` is a valid single-case union; prec.dynamic beats the alias reading.
                seq(
                    alias($.union_case_bare_fields, $.union_case),
                    repeat($.union_case),
                ),
            ),
        )),

        // No prec.right and no `repeat($.line_comment)` here: a shiftable comment would commit
        // `type A = int // c` to the bare-union reading and strand the comment in an ERROR.
        union_case_bare: $ => seq(
            field('name', $.identifier),
        ),

        union_case_bare_fields: $ => prec.dynamic(2, prec.right(seq(
            field('name', $.identifier),
            "of", choice(
                $.union_case_named_fields,
                field('fields', $.type_expression),
            ),
            repeat($.line_comment),
        ))),

        // `repeat($.line_comment)` absorbs trailing comments so the next case's `|` is the
        // one-token lookahead; otherwise the parser reduces `union_type_defn` early.
        union_case: $ => prec.right(choice(
            // `_case_docs_open` is anchored at the doc line so the node's extent starts at the docs;
            // ungated, a doc after the last case shifts into a phantom next case.
            seq($._case_docs_open, repeat1($.xml_doc_comment), field('decl', alias($._union_case_core, $.union_case))),
            $._union_case_core,
        )),

        _union_case_core: $ => prec.right(seq(
            "|",
            repeat($.attribute),
            field('name', $.identifier),
            optional(choice(
                seq("of", choice(
                    $.union_case_named_fields,
                    field('fields', $.type_expression),
                )),
                // Full-signature case form: `| Some : Value:'T -> 'T option`.
                seq(":", field('fields', $.type_expression)),
            )),
            repeat($.line_comment),
        )),

        // Named and anonymous fields mix freely (`int * inProgress: bool`); at least one is named,
        // and the all-anonymous form goes through the `type_expression` branch of `union_case`.
        union_case_named_fields: $ => prec.dynamic(1, seq(
            repeat(seq(field('fields', $._union_field_type), "*")),
            prec.dynamic(2, alias($.labelled_type, $.union_case_field)),
            repeat(seq("*", choice(
                prec.dynamic(2, alias($.labelled_type, $.union_case_field)),
                field('fields', $._union_field_type),
            ))),
        )),

        // Excludes tuple_type so a `*` between fields is always a field separator.
        _union_field_type: $ => choice(
            $.function_type,
            $._atomic_type,
            $.anonymous_record_type,
            $.struct_anonymous_record_type,
        ),

        // `name: T` in a member signature tuple (`abstract M: Context * selector: string -> Ret`);
        // the element type excludes `function_type`/`tuple_type` so `->` and `*` stay structural.
        labelled_type: $ => seq(
            // Both gates keep a bare identifier in type positions from forking the LR states.
            choice(seq($._label_attr, repeat1($.attribute)), $._label_gate),
            optional("?"),
            field('name', $.identifier),
            ":",
            field('type', choice(
                $._atomic_type,
                $.anonymous_record_type,
                $.struct_anonymous_record_type,
                $.flexible_type,
                // No nullable `T | null` here: in a named union field `|` is the case separator.
            )),
        ),

        // The first case may omit its `|`: `type Flags = A = 1uy | B = 2uy`.
        enum_type_defn: $ => choice(
            repeat1($.enum_case),
            seq(alias($._enum_case_bare, $.enum_case), repeat($.enum_case)),
        ),

        struct_type_defn: $ => seq("struct", repeat(seq($._class_body_member, optional(";"))), "end"),

        class_type_defn: $ => seq("class", repeat(seq($._class_body_member, optional(";"))), "end"),

        interface_type_defn: $ => seq("interface", repeat($._class_body_member), "end"),

        delegate_type_defn: $ => seq(
            "delegate",
            "of",
            field('arg_type', $.type_expression),
            "->",
            field('return_type', $.type_expression),
        ),

        enum_case: $ => choice(
            seq($._case_docs_open, repeat1($.xml_doc_comment), field('decl', alias($._enum_case_core, $.enum_case))),
            $._enum_case_core,
        ),

        _enum_case_core: $ => seq(
            "|",
            repeat($.attribute),
            field('name', $.identifier),
            "=",
            // `(1uL <<< 9)`: computed flag values.
            field('value', choice($.int_literal, $.negative_literal, $.char_literal, $.parenthesized_expression)),
        ),
        _enum_case_bare: $ => seq(
            field('name', $.identifier),
            "=",
            field('value', choice($.int_literal, $.negative_literal, $.char_literal, $.parenthesized_expression)),
        ),

        anonymous_record_type: $ => seq(
            "{|",
            $.record_type_field,
            repeat(prec.dynamic(TYPE_PREC.POSTFIX + 1, seq(optional(";"), $.record_type_field))),
            optional(";"),
            "|}",
        ),

        // The indented form stops a field's type from absorbing the next field's name
        // across a newline (`unit -> unit` then `A : 'A` read as postfix `unit A`).
        record_type_defn: $ => seq(
            // Docs between `=` and `{` attach to the record definition.
            repeat($.xml_doc_comment),
            optional($.access_modifier),
            "{",
            indentedOrInlineFieldList($, $.record_type_field, TYPE_PREC.POSTFIX + 1, { sameLineBraceForm: true }),
            "}",
        ),

        // prec(POSTFIX) ties the field's reduce with postfix_type's shift, so the
        // prec.dynamic in record_type_defn decides.
        record_type_field: $ => choice(
            seq(repeat1($.xml_doc_comment), field('decl', alias($._record_field_core, $.record_type_field))),
            $._record_field_core,
        ),

        _record_field_core: $ => prec(TYPE_PREC.POSTFIX, seq(
            repeat($.attribute),
            optional("mutable"),
            field('name', $.identifier),
            ":",
            field('type', $.type_expression),
        )),

        _literal: $ => choice(
            $.measure_literal,
            $.from_end_index,
            $.int_literal,
            $.float_literal,
            $.char_literal,
            $.string_literal,
            $.verbatim_string,
            $.triple_quoted_string,
            $.interpolated_string,
            $.interpolated_verbatim_string,
            $.interpolated_triple_string,
            $.multidollar_string,
            $.bool_literal,
            $.unit,
            $.null_literal,
        ),

        _expression: $ => choice(
            $.parenthesized_expression,
            $.inline_il_expression,
            $.typed_expression,
            $.application_expression,
            $.binary_expression,
            $.type_application_expression,
            $.unary_expression,
            $.deref_expression,
            $.prefix_bang_expression,
            $.active_pattern_expression,
            $.operator_application,
            $._operator_value,
            $.qualified_operator_expression,
            $.not_function,
            $.list_expression,
            $.array_expression,
            $.record_expression,
            $.anonymous_record_expression,
            $.struct_anonymous_record_expression,
            $.tuple_expression,
            $._literal,
            $.long_identifier,
            $.if_expression,
            $.match_expression,
            $.lambda_expression,
            $.let_expression,
            // Merging `use_expression` into `use_binding` creates an `_expression` vs `_token` conflict.
            alias($.use_expression, $.use_binding),
            $.computation_expression,
            $.for_expression,
            $.while_expression,
            $.dot_expression,
            $.dynamic_expression,
            $.index_expression,
            $.bracket_index_expression,
            $.try_expression,
            $.prefix_keyword_expression,
            $.do_expression,
            $.begin_end_expression,
            $.function_expression,
            $.typecast_expression,
            $.keyword_cast_expression,
            $.srtp_call_expression,
            $.nameof_expression,
            $.new_expression,
            $.object_expression,
            $.object_construction_expression,
            // Also valid in if/match branches inside CEs.
            $.ce_result_expr,
            $.struct_tuple_expression,
            $.typed_quotation,
            $.untyped_quotation,
            $.optional_named_arg,
            $.address_of_expression,
            $.type_keyword_expression,
            $.sequence_expression,
        ),

        // The scanner never emits `_layout_semi` inside brackets, and it consumes a trailing `;`
        // after a layout body's last statement into `_layout_end`, so `optional(";")` never fires.
        sequence_expression: $ => prec.dynamic(1, prec.left(PREC.SEQ_EXPR, seq(
            $._expression,
            repeat1(seq(choice($._layout_semi, ";"), $._expression)),
        ))),

        struct_tuple_expression: $ => prec(PREC.PAREN_EXPR, seq(
            "struct",
            "(",
            $._expression,
            ",",
            $._expression,
            repeat(seq(",", $._expression)),
            ")",
        )),

        // The compound `;@>` closer absorbs a trailing `;` (`<@ 1; 2; @>`) that a grammar-level
        // optional never catches. token(prec(1)) keeps `@>.` in `<@ e @>.Type` from lexing as a symbolic_op.
        typed_quotation: $ => prec(PREC.PAREN_EXPR, seq("<@", choice($._expression, $.type_ascription_expression),
            choice(alias(token(prec(1, "@>")), "@>"),
                   alias(token(seq(";", /[ \t\r\n]*/, "@>")), "@>")))),

        untyped_quotation: $ => prec(PREC.PAREN_EXPR, seq("<@@", choice($._expression, $.type_ascription_expression),
            choice(alias(token(prec(1, "@@>")), "@@>"),
                   alias(token(seq(";", /[ \t\r\n]*/, "@@>")), "@@>")))),

        optional_named_arg: $ => seq("?", $.identifier),

        address_of_expression: $ => prec(PREC.PREFIX_EXPR, seq("&", $._prefix_operand)),

        // The keyword is fused with its adjacent `<` so `typeof`, `sizeof`, `typedefof` stay
        // plain identifiers elsewhere (`let typeof = ...`, `typeof < x`).
        type_keyword_expression: $ => seq(
            alias(
                token(seq(choice("sizeof", "typeof", "typedefof"), "<")),
                $.type_intrinsic,
            ),
            $.type_expression,
            ">",
        ),

        // The lexer cannot see whitespace, so a spaced `a < b > c` reads as `a<b> c`.
        // prec.dynamic(1): when both readings survive a layout body, generic application wins.
        type_application_expression: $ => prec.dynamic(1, seq(
            $.long_identifier,
            "<",
            $.type_expression,
            repeat(seq(",", $.type_expression)),
            ">",
        )),

        // `_paren_block_open` fires only when a newline follows the `(`.
        parenthesized_expression: $ => seq("(", choice(
            seq($._paren_block_open, choice($._expression, $.type_ascription_expression), $._layout_end),
            $._expression,
        ), ")"),

        // `(#`/`#)` are glued tokens so they do not collide with a parenthesised flexible type `(#Foo)`.
        inline_il_expression: $ => seq(
            token(prec(2, "(#")),
            $.string_literal,
            // `type ('T)` IL type arguments: `(# "unbox.any !0" type ('T) x : 'T #)`.
            repeat(choice($._simple_expression, seq("type", "(", $.type_expression, ")"))),
            optional(seq(":", $.type_expression)),
            token(prec(2, "#)")),
        ),

        typed_expression: $ => seq("(", $._expression, ":", $.type_expression, ")"),

        application_expression: $ => prec.left(PREC.APP_EXPR, seq(
            $._expression,
            $._simple_expression,
        )),

        // A dedicated rule: making `operator_name` a general application head disturbs the
        // SRTP member constraints that share the `(` opener.
        operator_application: $ => prec.left(PREC.APP_EXPR, seq(
            alias($._value_operator_name, $.operator_name),
            repeat1($._simple_expression),
        )),

        _operator_value: $ => prec(-1, alias($._value_operator_name, $.operator_name)),

        // Excludes bare `^` / `&` / `|`: they collide with the SRTP `(^T ...)`, byref `(& ...)`
        // and active-pattern `(| ...)` openers.
        _value_operator_name: $ => seq(
            "(",
            choice($.symbolic_op, $.bang_op, "+", "-", "*", "/", "%", "=", "<", ">",
                "$", "?",
                "~~~"),
            ")",
        ),

        _simple_expression: $ => choice(
            $.parenthesized_expression,
            $.srtp_call_expression,
            $.address_of_expression,
            $.deref_expression,
            $.prefix_bang_expression,
            $.inline_il_expression,
            $.typed_expression,
            $.list_expression,
            $.array_expression,
            $.record_expression,
            $.anonymous_record_expression,
            $.struct_anonymous_record_expression,
            // Must sit beside struct_anonymous_record_expression: once `struct` is shiftable
            // here via one rule, the other must be too.
            $.struct_tuple_expression,
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
            $.multidollar_string,
            $.bool_literal,
            $.unit,
            $.null_literal,
            $.long_identifier,
            $.type_application_expression,
            $.operator_name,
            $.active_pattern_expression,
            $.qualified_operator_expression,
            $.not_function,
            $.object_expression,
            $.object_construction_expression,
            // `printfn "a"; async { ... }`: a CE after a statement reads as a chained application.
            $.computation_expression,
            // Without this, `<@` after a value lexes as the `<@` symbolic_op.
            $.typed_quotation,
            $.untyped_quotation,
        ),

        // One rule for all infix operators keeps the post-_expression state count down.
        binary_expression: $ => choice(
            prec.left(PREC.PIPE_EXPR,      seq(field('left', $._expression), field('operator', choice("|>", "<|", ">>", "<<")), field('right', $._expression))),
            prec.left(PREC.BOOL_OR,        seq(field('left', $._expression), field('operator', "||"), field('right', $._expression))),
            prec.left(PREC.BOOL_AND,       seq(field('left', $._expression), field('operator', "&&"), field('right', $._expression))),
            // `_infix_block_open` fires only when the operator ends a line and the next line is deeper.
            prec.left(PREC.BOOL_OR,        seq(field('left', $._expression), field('operator', "||"), $._infix_block_open, field('right', $._expression), $._layout_end)),
            prec.left(PREC.BOOL_AND,       seq(field('left', $._expression), field('operator', "&&"), $._infix_block_open, field('right', $._expression), $._layout_end)),
            prec.left(PREC.ADDITIVE,       seq(field('left', $._expression), field('operator', choice("+", "-")), field('right', $._expression))),
            prec.left(PREC.MULTIPLICATIVE, seq(field('left', $._expression), field('operator', choice("*", "/", "%")), field('right', $._expression))),
            prec.left(PREC.INFIX_OP,       seq(field('left', $._expression), field('operator', choice(">", "<", ">=", "<=", "=", "<>")), field('right', $._expression))),
            prec.right(PREC.INFIX_OP,      seq(field('left', $._expression), field('operator', "::"), field('right', $._expression))),
            prec.left(PREC.INFIX_OP,       seq(field('left', $._expression), field('operator', $.symbolic_op), field('right', $._expression))),
            // `?` has no spaced-infix form: `f ? x` is ambiguous with the optional named arg `f ?x`.
            prec.left(PREC.INFIX_OP,       seq(field('left', $._expression), field('operator', alias("$", $.symbolic_op)), field('right', $._expression))),
            // `^` operators are right-associative in F#; `^ident` lexes as a typar (longer match).
            prec.right(PREC.INFIX_OP,      seq(field('left', $._expression), field('operator', alias("^", $.symbolic_op)), field('right', $._expression))),
            prec.right(PREC.LARROW,        seq(field('left', $._expression), field('operator', "<-"), field('right', $._expression))),
            // TUPLE_EXPR + 1: in `f.['a'..'z', *]` the `,` separates index dimensions.
            prec.right(PREC.TUPLE_EXPR + 1, seq(field('left', $._expression), field('operator', ".."), field('right', $._expression))),
        ),

        // The body-less branch keeps a mid-edit `fun x ->` a `lambda_expression` node for Helix indent.
        lambda_expression: $ => prec.right(PREC.FUN_EXPR,
            choice(
                prec(2, seq(
                    "fun",
                    repeat1($.parameter),
                    optional($._lambda_return_annot),
                    "->",
                    field('body', $._indented_or_inline_body),
                )),
                prec(1, seq(
                    "fun",
                    repeat1($.parameter),
                    optional($._lambda_return_annot),
                    "->",
                )),
            ),
        ),

        // `fun (x: int) : int -> x`: an arrow-free type, so a function_type cannot swallow the `->`.
        _lambda_return_annot: $ => prec(1, seq(
            ":",
            $._atomic_type,
        )),

        unary_expression: $ => prec(PREC.PREFIX_EXPR, seq(
            // `!` is not here: it binds tighter than application (`deref_expression`), these do not
            // (`f - x` is a subtraction). Lexical prec -1: in `(~-)` the operator_name's symbolic_op must win.
            choice("not", "~~~", "-", "+", "%%", "%",
                   alias(token(prec(-1, /~[!$%&*+\-.\/<=>?@^|~]+/)), $.symbolic_op)),
            $._expression,
        )),

        // `f !cell` is `f (!cell)`: deref binds tighter than application.
        deref_expression: $ => prec(PREC.PREFIX_EXPR, seq("!", $._prefix_operand)),

        // Not `dot_expression` itself: its application object would let `!! 1 m` shift into an application and die.
        _prefix_operand: $ => choice(
            $._simple_expression,
            prec(1, alias(seq($.parenthesized_expression, repeat1(seq(".", $.identifier))), $.dot_expression)),
        ),

        // A `!`-led prefix operator (`!! "*.fs"`) binds tighter than application: `!! 1 m` is `((!!) 1) m`.
        prefix_bang_expression: $ => prec(PREC.PREFIX_EXPR, seq(field('operator', $.bang_op), $._prefix_operand)),
        bang_op: _ => token(/![!$%&*+\-.\/<=>?@^|~]+/),

        // `not` is an ordinary function value (`not >> g`); prec(-1) so `not x` stays a unary_expression.
        not_function: _ => prec(-1, "not"),

        // 2+ chars; single-char operators are their own tokens. `/` is in the class so
        // `(!//!)` is an operator, not a line comment. `<@`/`@>` string tokens win over this.
        symbolic_op: _ => token(choice(
            "@",
            // `:` is not an operator char (type annotations), so `:=` is listed explicitly.
            ":=",
            /[!$%&*+\-.\/<=>?@^|~][!$%&*+\-.\/<=>?@^|~]+/,
        )),

        list_expression: $ => seq(
            "[",
            optional(choice(
                // `_bracket_semi` is a dedicated token a nested sequence cannot absorb,
                // so newline-aligned elements never chain into one application.
                $._block_elements,
                // The inline `;` separator has a static prec above SEQ_EXPR: `[ a; b ]` is two elements.
                seq(
                    $._inline_elements,
                ),
            )),
            "]",
        ),

        array_expression: $ => seq(
            "[|",
            optional(choice(
                $._block_elements,
                seq(
                    $._inline_elements,
                ),
            )),
            "|]",
        ),

        // Same prec as struct_tuple_expression so the shared `struct` resolves on the next token.
        struct_anonymous_record_expression: $ => prec(PREC.PAREN_EXPR, seq("struct", $.anonymous_record_expression)),
        struct_anonymous_record_type: $ => seq("struct", $.anonymous_record_type),

        anonymous_record_expression: $ => seq(
            "{|",
            choice(
                seq(
                    field('base', choice($._simple_expression, $.application_expression, $.bracket_index_expression, $.index_expression, $.dot_expression)),
                    "with",
                    $._record_fields,
                ),
                $._record_copy_block,
                $._record_fields,
                // `{| |}` is a valid empty anonymous record.
                blank(),
            ),
            "|}",
        ),

        record_expression: $ => seq(
            "{",
            choice(
                seq(
                    field('base', choice($._simple_expression, $.application_expression, $.bracket_index_expression, $.index_expression, $.dot_expression)),
                    "with",
                    $._record_fields,
                ),
                // Base on its own line after `{`: the scanner emits `_layout_open` at the base's column.
                $._record_copy_block,
                $._record_fields,
            ),
            "}",
        ),

        _record_fields: $ => indentedOrInlineFieldList($, $.record_field, PREC.APP_EXPR + 1, { sameLineBraceForm: true }),

        // prec(APP_EXPR) lets the callers' prec.dynamic prefer a new field over extending
        // the value by application.
        record_field: $ => prec(PREC.APP_EXPR, seq(
            field('name', $.long_identifier),
            "=",
            choice(
                seq($._field_block_open, field('value', $._expression), $._layout_end),
                field('value', $._expression),
            ),
        )),

        tuple_expression: $ => prec.left(PREC.TUPLE_EXPR, seq(
            $._expression,
            ",",
            $._expression,
            repeat(seq(",", $._expression)),
        )),

        // `_expr_open` pushes the body's first-token column, inline or on its own line. A bare
        // `_expression` alternative would let an inline body reduce without a layout close.
        _indented_or_inline_body: $ => seq($._expr_open, choice($._expression, $.type_ascription_expression), $._layout_end),

        // Only `_then_open` bodies close at a mid-line `else`; in `do x <- if c then a else b`
        // the `else` belongs to the inner `if`.
        _then_body: $ => seq($._then_open, choice($._expression, $.type_ascription_expression), $._layout_end),

        // The body-less branch keeps a mid-edit `if c then` an `if_expression` node for Helix indent.
        if_expression: $ => prec.right(PREC.IF_EXPR, choice(
            prec(2, seq(
                "if",
                $._expression,
                "then",
                $._then_body,
                // The scanner suppresses `_else_open` before `if`, so `else if` flattens here
                // (a nested if in an else body would over-close at a later dedented `elif`).
                repeat(seq(choice("elif", seq("else", "if")), $._expression, "then", $._then_body)),
                // `... else None : 'a option`: the `:` binds the whole `if` in F#, same span here.
                optional(seq("else", $._else_open, field('else', choice($._expression, $.type_ascription_expression)), $._layout_end)),
            )),
            prec(1, seq(
                "if",
                $._expression,
                "then",
            )),
        )),

        // Single-char alternatives are listed because symbolic_op requires 2+ chars.
        operator_name: $ => seq(
            "(",
            choice(
                $.symbolic_op,
                $.bang_op,
                "+", "-", "*", "/", "%",
                "=", "<", ">",
                "&", "|", "^",
                "$", "~", "!", "?",
                "~~~",   // the string token out-lexes symbolic_op's regex at equal length
                "*",     // `( * )`: spaced to dodge the `(*` comment opener
                seq("..", ".."),            // `(.. ..)` stepped range
                seq(".", $.unit),           // `(.())` custom indexer
                seq(".", $.unit, "<-"),     // `(.()<-)` custom indexed setter
                alias(token(">:"), $.symbolic_op),   // only lexable here (`F<'T>: 'T` elsewhere)
            ),
            ")",
        ),

        // The qualified tail `.(|Name|)` is one token so it never competes with a long_identifier's `.`.
        active_pattern_expression: $ => choice(
            $.active_pattern_name,
            seq($.long_identifier, $.active_pattern_member),
        ),

        active_pattern_member: _ => token(seq(
            ".",
            "(|",
            /([\p{L}_][\p{L}\p{Nd}_']*|``[^`\n\r]+``)/,
            repeat(seq("|", /([\p{L}_][\p{L}\p{Nd}_']*|``[^`\n\r]+``)/)),
            optional(seq("|", "_")),
            "|)",
        )),

        // `Unchecked.(+)` / `'T.(+)` (IWSAM); the tail `.(+)` is one token, as in active_pattern_member.
        qualified_operator_expression: $ => seq(choice($.long_identifier, $.type_parameter), $.operator_member),
        operator_member: _ => token(seq(".", "(", /[ \t]*/, /[!%&*+\-./<=>?@^|~$?:]+/, /[ \t]*/, ")")),

        _layout_body: $ => seq($._layout_open, field('body', $._ascribable_body), $._layout_end),

        _try_body_ascribable: $ => seq($._try_open, field('body', choice($._expression, $.type_ascription_expression)), $._layout_end),

        _try_body: $ => seq($._try_open, $._expression, $._layout_end),

        _for_body: $ => seq($._for_open, field('body', $._expression), $._layout_end),

        // The scanner opens a `(` block for any content on a following line, so this must accept it.
        _paren_args: $ => seq("(", choice(
            seq($._paren_block_open, $._expression, repeat(seq(",", $._expression)), $._layout_end),
            optional(seq($._expression, repeat(seq(",", $._expression)))),
        ), ")"),

        _block_elements: $ => seq($._bracket_open, $._expression, repeat(prec(PREC.PAREN_EXPR, seq(choice(";", $._bracket_semi), $._expression))), optional(choice(";", $._bracket_semi)), $._bracket_close),

        _inline_elements: $ => seq($._expression, repeat(prec(PREC.PAREN_EXPR, seq(";", $._expression))), optional(prec(PREC.PAREN_EXPR, ";"))),

        _with_members: $ => choice($._class_body_block, $._class_body_member),

        _block_pattern_items: $ => seq($._bracket_open, $._list_pattern_item, repeat(seq(choice(";", $._bracket_semi), $._list_pattern_item)), optional(choice(";", $._bracket_semi)), $._bracket_close),

        _class_body_block: $ => seq($._layout_open, repeat($._class_body_member), $._layout_end),

        _record_copy_block: $ => seq($._layout_open, field('base', choice($._simple_expression, $.application_expression, $.bracket_index_expression, $.index_expression, $.dot_expression)), "with", $._record_fields, $._layout_end),

        // A separate rule so the head's optional modifiers do not multiply its parser states.
        _let_rhs: $ => prec.right(seq("=", choice($._layout_body, $._preproc_break), repeat($.let_and_binding))),
        _let_rhs_bodiless: $ => prec.right(seq("=", repeat($.let_and_binding))),

        _use_rhs: $ => prec.right(seq(optional(seq(":", $.type_expression)), "=", $._layout_body)),

        _ctor_rhs: $ => prec.right(seq("=", $._layout_body, optional(seq("then", $._layout_body)))),

        _member_val_rhs: $ => prec.right(seq(optional($._return_type_annot), "=", $._try_body_ascribable, optional($.auto_property_accessors))),

        _val_tail: $ => seq(":", $.type_expression, optional(seq("=", $._literal))),

        _abstract_tail: $ => prec.right(seq(optional($.type_parameter_list), ":", $.type_expression, optional($.auto_property_accessors))),

        _module_rhs: $ => seq("=", optional(choice(
            prec.dynamic(1, field('abbrev', $.long_identifier)),
            seq("begin", repeat($._token), "end"),
            seq($._block_open, repeat($._token), $._layout_end),
        ))),

        _accessor_rhs: $ => seq(optional($._return_type_annot), "=", $._layout_body),

        _srtp_member_sig: $ => seq(
            "(",
            choice(
                seq(optional("static"), "member", field('member_name', choice($.identifier, $.operator_name))),
                field('member_name', "new"),
            ),
            ":",
            field('member_type', $.type_expression),
            ")",
        ),
        _srtp_term: $ => choice($.type_parameter, $.long_identifier, $.generic_type),

        // No or-pattern binder: via `pattern` it overlaps the constructor-application binder.
        _for_binder: $ => choice($.identifier, $.wildcard_pattern, $.tuple_pattern, $.record_pattern,
                        $.struct_tuple_pattern,
                        $.as_pattern,
                        $.literal_pattern, $.array_pattern, $.list_pattern,
                        $.tuple_typed_first_pattern,
                        $.typed_pattern,
                        $.tuple_typed_pattern,
                        // `for k: T, r in ...`: unparenthesised tuple binder with typed elements.
                        seq($._tuple_pattern_item, repeat1(seq(",", $._tuple_pattern_item))),
                        $.named_field_pattern,
                        // `for KeyValue(k, v) in dict do ...`
                        prec.right(1, seq($.long_identifier, repeat1($._tuple_elem_pattern)))),

        _let_name_pattern: $ => choice(
            $.identifier, $.operator_name, $.active_pattern_name,
            $.typed_pattern, $.tuple_pattern, $.tuple_typed_first_pattern, $.struct_tuple_pattern, $.unparenthesized_tuple_pattern, alias($.ctor_first_tuple_pattern, $.unparenthesized_tuple_pattern), $.record_pattern, $.list_pattern, $.array_pattern, $.wildcard_pattern,
            alias(prec.right(2, seq($._tuple_elem_pattern, "::", $.pattern)), $.cons_pattern),
            alias(prec.left(1, seq($._tuple_elem_pattern, "|", $.pattern)), $.or_pattern),
            alias(prec.left(1, seq($._tuple_elem_pattern, "&", $.pattern)), $.and_pattern),
            $.type_check_pattern,   // `let :? T = ...` (FS0025 is only a warning)
            // Only `()`, not a general literal pattern: `let 0leaderingzero = ...` would parse as `let 0 leaderingzero`.
            $.unit,
            // `_ctor_tuple_gate`: ungated, the `(` after any `let name` shifts into this path.
            seq($._ctor_tuple_gate, $.named_field_pattern),
            alias(seq($._ctor_tuple_gate, $.named_field_pattern, "as", $.identifier), $.as_pattern),
            alias(seq($._ctor_tuple_gate, $.long_identifier, $.tuple_pattern, "as", $.identifier), $.as_pattern),
            // as_tuple_elem_pattern wins the lex-prec race over as_pattern, so it must be a valid name here.
            alias($.as_tuple_elem_pattern, $.as_pattern),
        ),

        _let_signature: $ => seq(
            optional(choice("inline", "mutable")),
            optional($.access_modifier),
            field('name', $._let_name_pattern),
            optional($.type_parameter_list),
            field('parameters', repeat($.parameter)),
            optional($._return_type_annot),
        ),

        // A dedicated body-less branch, not `optional(body)`: tree-sitter prefers the shorter parse,
        // and a mid-edit `let x =` must stay a `let_binding` node (`!body` in indents.scm targets it).
        let_binding: ($) => prec.right(PREC.LET_DECL, choice(
            seq(repeat1($.xml_doc_comment), field('decl', alias($._let_binding_core, $.let_binding))),
            $._let_binding_core,
        )),

        _let_binding_core: ($) => prec.right(PREC.LET_DECL, choice(
            prec(2, prec.dynamic(1, seq(
                repeat($.attribute),
                optional("static"),
                "let",
                optional(token.immediate("!")),
                optional("rec"),
                decoration($),
                $._let_signature,
                $._let_rhs,
            ))),
            prec(1, prec.dynamic(1, seq(
                repeat($.attribute),
                optional("static"),
                "let",
                optional(token.immediate("!")),
                optional("rec"),
                decoration($),
                $._let_signature,
                $._let_rhs_bodiless,
            ))),
        )),

        // A bare `_expression` body would absorb the following sibling `let` as a let_expression continuation.
        let_and_binding: ($) => prec.right(PREC.LET_DECL, choice(
            seq($._and_docs, field('decl', alias($._let_and_core, $.let_and_binding))),
            $._let_and_core,
        )),

        _let_and_core: ($) => prec.right(PREC.LET_DECL, choice(
            prec(2, seq("and", optional(token.immediate("!")), repeat($.attribute), $._let_signature, "=",
                $._layout_body,
            )),
            prec(1, seq("and", optional(token.immediate("!")), repeat($.attribute), $._let_signature, "=")),
        )),

        let_decl_indented: ($) => seq(
            "let",
            optional(token.immediate("!")),
            optional("rec"),
            $._let_signature,
            "=",
            // An `_expr_open` body closes at an inline `in` (`let x = e in body`).
            field('body', $._indented_or_inline_body),
            // Each `and` body needs the same layout wrapper, or it absorbs the continuation line.
            repeat(alias($._and_decl_indented, $.let_and_binding)),
        ),

        _and_decl_indented: ($) => choice(
            seq($._and_docs, field('decl', alias($._and_decl_core, $.let_and_binding))),
            $._and_decl_core,
        ),

        _and_decl_core: ($) => seq(
            "and",
            optional(token.immediate("!")),
            repeat($.attribute),
            $._let_signature,
            "=",
            choice(
                $._layout_body,
                $._layout_body,
            ),
        ),

        // The second branch is needed for the single-line `let x = body in continuation` form.
        let_expression: ($) => prec.right(PREC.LET_EXPR,
            choice(
                seq(
                    field('binding', $.let_decl_indented),
                    // The scanner closes the body at an inline `in`; the keyword itself is consumed here.
                    optional("in"),
                    field('continuation', $._expression),
                ),
                seq(
                    "let",
                    optional("rec"),
                    $._let_signature,
                    "=",
                    $._expression,
                    "in",
                    $._expression,
                ),
            ),
        ),

        use_expression: $ => prec.right(PREC.LET_EXPR,
            seq("use", optional(token.immediate("!")), optional("mutable"), field('name', $._use_name), $._use_rhs),
        ),

        // Pure-identifier chains (`A.B.C`) belong to `long_identifier` only; the object is a compound expression.
        dot_expression: $ => prec(PREC.DOT, seq(
            field('object', $._dot_object),
            ".",
            field('member', choice($.identifier, $.int_literal)),   // `.1`: tuple item (`cons.( :: ).1`)
        )),

        // `token.immediate("?")` keeps `obj?member` distinct from an `optional_named_arg` (`f ?name = x`).
        dynamic_expression: $ => prec(PREC.DOT, seq(
            field('object', $._expression),
            choice(
                token.immediate("?"),
                // F# lexes `?` followed by whitespace as the binary `(?)` operator.
                alias(token(seq("?", /[ \t]+/)), "?"),
            ),
            field('member', choice($.identifier, $.parenthesized_expression)),
        )),

        // Excludes long_identifier/identifier so pure-identifier chains never split into dot_expressions.
        _dot_object: $ => choice(
            $.parenthesized_expression,
            $.typed_expression,
            $.struct_tuple_expression,
            $.index_expression,
            $.bracket_index_expression,
            $.application_expression,
            $.dynamic_expression,
            $.object_expression,
            $.object_construction_expression,
            $.list_expression,
            $.array_expression,
            $.record_expression,
            $.anonymous_record_expression,
            $.struct_anonymous_record_expression,
            $.typed_quotation,
            $.untyped_quotation,
            $.qualified_operator_expression,
            $.type_application_expression,
            $.type_keyword_expression,
            $.dot_expression,
            $.begin_end_expression,
            // `'T.StaticMember` (SRTP member access on a type parameter).
            $.type_parameter,
            // `"abc".Length` / `42 .ToString()` (`42.ToString()` with no space is a float in F#).
            $._literal,
        ),

        // `.[` is a single terminal so it never conflicts with the `.` in long_identifier.
        index_expression: $ => prec(PREC.INDEX_EXPR, seq(
            field('object', $._expression),
            ".[",
            field('index', $._index_args),
            "]",
        )),

        // `token.immediate("[")`: no space before `[` distinguishes `arr[i]` from `f [i]`.
        bracket_index_expression: $ => prec(PREC.INDEX_EXPR, seq(
            field('object', $._expression),
            token.immediate("["),
            field('index', optional($._index_args)),   // `TreeNode[]`: an empty list argument
            "]",
        )),

        _index_args: $ => seq(
            choice($._index_arg, "*"),   // `m[*, 2]`: whole-dimension slice
            repeat(seq(",", choice($._index_arg, "*"))),
        ),

        // prec(TUPLE_EXPR + 1): in `m.[..0, ..1]` the `,` separates dimensions, not a tuple.
        index_slice: $ => prec.dynamic(PREC.DOTDOT_SLICE, prec(PREC.TUPLE_EXPR + 1, choice(
            seq($._expression, ".."),
            seq("..", $._expression),
        ))),

        _index_arg: $ => choice(
            $.index_slice,
            prec(PREC.TUPLE_EXPR + 1, $._expression),
        ),

        // --- Type casts ---
        // `o :?> byte[]` relies on array_type's prec'd immediate `[`.
        typecast_expression: $ => prec(PREC.TYPED_EXPR,
            seq($._expression, choice(":>", ":?>", ":?"), $.type_expression),
        ),

        // `body : Type` on a binding/member body tail. Reached via `_ascribable_body`, not
        // `_expression`, so it does not shadow `typed_expression` or turn a trailing `T[]` into an index.
        type_ascription_expression: $ => prec.right(seq($._expression, ":", $.type_expression, optional($._when_constraints))),

        // A trailing `;` after the body is a no-op statement terminator.
        _ascribable_body: $ => seq(
            choice(
                seq(
                    choice(
                        $._expression,
                        $.type_ascription_expression,
                        // `x : decimal |> f`: F# pipes the annotated value.
                        alias(seq(field('left', $.type_ascription_expression), field('operator', choice("|>", "||>", "|||>")), field('right', $._expression)), $.binary_expression),
                    ),
                    // Static optimization equations: `body`\n`when 'T : bool = (# "cgt" x y : int #)`.
                    repeat($.static_optimization),
                ),
                repeat1($.static_optimization),
            ),
            optional(";"),
        ),

        static_optimization: $ => seq(
            "when",
            $.type_parameter,
            ":",
            $.type_expression,
            repeat(seq("and", $.type_parameter, ":", $.type_expression)),
            "=",
            $._expression,
        ),

        keyword_cast_expression: $ => seq(choice("upcast", "downcast"), $._expression),

        // `( ^T : (member X: int) a )`; prec(PAREN_EXPR) beats `parenthesized_expression`.
        srtp_call_expression: $ => prec(PREC.PAREN_EXPR, choice(
            seq(
                "(",
                $.type_parameter,
                ":",
                $._srtp_member_sig,
                field('argument', $._expression),
                ")",
            ),
            // `((^a or ^b) : (member ...) arg)`
            seq(
                "(",
                "(",
                // A parenthesised identifier needs an `or`: `((float) x)` is an ordinary application.
                choice(
                    seq($.type_parameter, repeat(seq("or", $._srtp_term))),
                    seq($.long_identifier, repeat1(seq("or", $._srtp_term))),
                ),
                ")",
                ":",
                $._srtp_member_sig,
                field('argument', $._expression),
                ")",
            ),
        )),

        nameof_expression: $ => choice(
            seq("nameof", $._simple_expression),
            seq("nameof", token.immediate("<"), $.type_expression, ">"),
        ),

        // The type is not a full type_expression so the `(` is not consumed as a parenthesized_type.
        new_expression: $ => prec(PREC.NEW_OBJ,
            seq(
                "new",
                choice($.generic_type, $.long_identifier, $.type_parameter),
                choice(
                    seq(
                        "(",
                        // `new XElement(name : XName)`: the parens belong to the call, so typed_expression cannot match.
                        optional(seq(
                            choice($._expression, $.type_ascription_expression),
                            repeat(seq(",", choice($._expression, $.type_ascription_expression))),
                        )),
                        ")",
                    ),
                    // Not `_simple_expression`/`_literal`: their `unit` token would out-lex the
                    // `( )` pair above and turn every `new T()` into a unit-arg call.
                    $.int_literal, $.float_literal, $.char_literal,
                    $.string_literal, $.verbatim_string, $.triple_quoted_string,
                    $.interpolated_string, $.bool_literal, $.null_literal,
                    $.long_identifier,
                    $.parenthesized_expression,
                ),
            ),
        ),

        object_construction_expression: $ => seq(
            "{",
            $._record_open,
            $.inherit_decl,
            repeat(seq(choice(";", $._bracket_semi), $.record_field)),
            optional(choice(";", $._bracket_semi)),
            $._bracket_close,
            "}",
        ),

        object_expression: $ => seq(
            "{",
            "new",
            field('type', choice($.generic_type, $.long_identifier)),
            optional($._paren_args),
            optional(seq(
                "with",
                repeat($._class_body_member),
            )),
            "}",
        ),


        // --- Exceptions ---

        // P/Invoke: `extern bool private GetConsoleMode(void* _h, int* _mode)`; C-style
        // types with `*` / `&` / `[]` suffixes.
        extern_decl: $ => seq(
            "extern",
            field('return_type', alias($._extern_type, $.type_expression)),
            optional($.access_modifier),
            field('name', $.identifier),
            "(",
            optional(seq($._extern_param, repeat(seq(",", $._extern_param)))),
            ")",
        ),
        _extern_type: $ => prec.right(seq($.long_identifier, repeat(choice("*", "&", seq("[", "]"))))),
        _extern_param: $ => seq(repeat($.attribute), alias($._extern_type, $.type_expression), optional($.identifier)),

        exception_decl: $ => choice(
            seq(repeat1($.xml_doc_comment), field('decl', alias($._exception_decl_core, $.exception_decl))),
            $._exception_decl_core,
        ),

        _exception_decl_core: $ => prec.dynamic(1, seq(
            repeat($.attribute),
            "exception",
            optional($.access_modifier),
            field('name', $.identifier),
            optional(choice(
                seq("of", $.type_expression),
                // Exception abbreviation: `exception E2 = OtherE`.
                seq("=", $.long_identifier),
            )),
            optional($._type_augmentation),
        )),

        // `_match_open` is emitted right after `with`/`function` at the first arm's column;
        // `_match_end` fires on a dedent below it, or at it without a leading `|`.
        _match_arms: $ => prec.right(seq(
            $._match_open,
            $.match_arm,
            repeat($.match_arm),
            $._match_end,
        )),

        // `_try_open` bodies close at `with`/`finally`; a generic expr body must not,
        // or a nested `match ... with` would close it.
        try_expression: $ => prec.right(PREC.MATCH_EXPR, seq(
            "try",
            $._try_body,
            choice(
                seq("with", $._match_arms),
                seq("finally", $._try_body),
            ),
        )),

        prefix_keyword_expression: $ => prec(PREC.PREFIX_EXPR, choice(
            seq(choice("lazy", "assert", "fixed"), $._expression),   // `use p = fixed arr`
            // `_lazy_open` declines inline bodies, so `lazy x` always takes the plain branch.
            seq("lazy", $._lazy_open, choice($._expression, $.type_ascription_expression), $._layout_end),
        )),

        // prec 2, just above SEQ_EXPR: `do e` reduces as one statement of the enclosing sequence.
        do_expression: $ => prec.right(2, seq("do", $._indented_or_inline_body)),

        // The scanner also closes an inline body at a mid-line `end`.
        begin_end_expression: $ => prec(PREC.PAREN_EXPR,
            seq("begin", $._indented_or_inline_body, "end")),

        function_expression: $ => prec.right(PREC.MATCH_EXPR,
            seq("function", $._match_arms),
        ),

        // --- For / While ---

        // `_for_open` is not emitted when the next token sits at the enclosing CE column
        // (`query { for x in xs do`\n`where ... }`): the body stays empty and the operators are siblings.
        for_expression: $ => prec.right(PREC.IF_EXPR, seq(
            "for",
            choice(
                seq(
                    $._for_binder,
                    "in", $._expression,
                    choice(
                        seq("do",
                            // No bare `optional($._expression)` fallback: it would eat the next query operator.
                            optional($._for_body), optional("done"),
                        ),
                        // `[ for x in xs -> expr ]`; prec.dynamic stops `->` lexing as a symbolic_op on the enumerable.
                        prec.dynamic(1, seq("->", field('body', $._expression))),
                    ),
                ),
                seq(
                    choice($.identifier, $.wildcard_pattern),
                    "=", $._expression, choice("to", "downto"), $._expression, "do",
                    optional($._for_body), optional("done"),
                ),
            ),
        )),

        while_expression: $ => prec.right(PREC.IF_EXPR, choice(
            prec(2, seq(
                "while", $._expression, "do",
                field('body', $._indented_or_inline_body), optional("done"),
            )),
            prec(1, seq("while", $._expression, "do")),
        )),

        // CE_EXPR < APP_EXPR: `f { field = val }` is an application with a record argument.
        computation_expression: $ => prec(PREC.CE_EXPR,
            seq(
                // No builder: `{1..3}` is sequence-range sugar. Safe because `_ce_brace_open` gates the fork.
                optional(choice(
                    field('builder', $.long_identifier),
                    // Builder-is-an-application CE: `div(attrs) { ... }`, `stage "x" { ... }`.
                    // `_element_dsl_open` is emitted only when `ident <arg> {` is ahead.
                    seq(
                        $._element_dsl_open,
                        field('builder', $.long_identifier),
                        field('args', choice($.unit, $.parenthesized_expression,
                                             $.string_literal, $.verbatim_string, $.triple_quoted_string,
                                             $.anonymous_record_expression)),
                        // Fluent method chain before the body: `div(attrs).hxTarget("#x") { ... }`.
                        repeat(seq(
                            ".",
                            field('method', $.long_identifier),
                            field('method_args', choice($.unit, $.parenthesized_expression)),
                        )),
                    ),
                )),
                // When the scanner declines `_ce_brace_open`, `head { ... }` parses as
                // application(head, record/object_expression) with the literal `{`.
                $._ce_brace_open,
                $._ce_body,
            ),
        ),

        _ce_body: $ => prec(PREC.CE_EXPR, seq(
                "{",
                optional(choice(
                    // `reserved('query_ce')` wraps only the statements, not the external bracket tokens.
                    seq(
                        $._bracket_open,
                        reserved('query_ce', $._ce_statement),
                        repeat(seq(choice(";", $._bracket_semi), reserved('query_ce', $._ce_statement))),
                        optional(choice(";", $._bracket_semi)),
                        $._bracket_close,
                    ),
                    seq(
                        reserved('query_ce', $._ce_statement),
                        repeat(prec(1, seq(";", reserved('query_ce', $._ce_statement)))),
                        optional(";"),
                    ),
                )),
                "}",
        )),

        _ce_statement: $ => choice(
            // Docs before un-slotted CE statements.
            prec.dynamic(-1, $.xml_doc_comment),
            $.use_binding,
            $.ce_match_bang_expr,
            $.let_binding,
            $.do_stmt,
            $.query_operator,
            $.query_join_operator,
            $.query_group_by_operator,
            $.query_left_outer_join_operator,
            // A query word as a variable at a statement start (`count <- count + 1`, `join (fun ...)`).
            alias(seq(field('left', alias($._query_op_word, $.identifier)), field('operator', "<-"), field('right', $._expression)), $.binary_expression),
            alias(seq(alias("join", $.identifier), repeat1($._simple_expression)), $.application_expression),
            $._expression,
        ),

        query_operator: $ => prec.right(seq(
            field('op', alias($._query_op_word, $.query_op)),
            optional($._expression),
        )),

        // One token instead of one symbol per word (a symbol is a column in every dense parser state).
        // Lexical prec 1 beats `identifier` at a CE statement start; elsewhere the word is an identifier.
        _query_op_word: _ => token(prec(1, choice("select", "where", "sortBy", "sortByDescending", "thenBy", "thenByDescending", "take", "skip", "takeWhile", "skipWhile", "distinct", "count", "head", "last", "exactlyOne", "minBy", "maxBy", "sumBy", "averageBy", "find", "exists", "all", "contains", "nth", "headOrDefault", "lastOrDefault", "exactlyOneOrDefault"))),


        query_join_operator: $ => seq(
            "join",
            field('name', $.identifier),
            "in",
            field('source', $._expression),
            "on",
            field('condition', $._expression),
        ),

        query_group_by_operator: $ => seq(
            choice("groupBy", "groupValBy", "groupJoin"),
            field('key', $._expression),
            optional(seq("into", field('into', $.identifier))),
        ),

        query_left_outer_join_operator: $ => seq(
            "leftOuterJoin",
            field('name', $.identifier),
            "in",
            field('source', $._expression),
            "on",
            field('condition', $._expression),
            "into",
            field('into', $.identifier),
        ),

        use_binding: $ => prec.right(PREC.LET_DECL,
            seq("use", optional(token.immediate("!")), optional("mutable"), field('name', $._use_name), $._use_rhs),
        ),

        // Wider pattern sets here cost ~150 parser states.
        _use_name: $ => choice($.identifier, $.typed_pattern),

        ce_match_bang_expr: $ => prec.right(PREC.MATCH_EXPR,
            seq("match!", $._expression, "with", $._match_arms),
        ),

        // Known gap: an inline trailing `;` before `}` (`seq { yield x; }`) - the `;` shift
        // into the operand's sequence statically beats this rule's reduce.
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
                // `match x: int option with`: the scrutinee may carry a bare ascription.
                choice($._expression, $.type_ascription_expression),
                "with",
                $._match_arms,
            ),
        ),

        // `| 1, 2 | 3, 4 ->` yields four sibling patterns; prec(2) beats or_pattern's prec(1).
        match_arm: ($) => seq(
            optional("|"),
            choice($.pattern, $.cons_typed_head_pattern),
            repeat(prec(2, seq(choice(",", "|"), $.pattern))),
            optional(seq("when", $._expression)),
            "->",
            field('body', $._layout_body),
        ),

        pattern: $ => choice(
            $.wildcard_pattern,
            $.literal_pattern,
            $.identifier_pattern,
            $.cons_pattern,
            $.or_pattern,
            $.and_pattern,
            $.tuple_pattern,
            $.tuple_typed_first_pattern,
            $.struct_tuple_pattern,
            $.typed_pattern,
            $.as_pattern,
            $.list_pattern,
            $.array_pattern,
            $.type_check_pattern,
            $.record_pattern,
            $.named_field_pattern,
        ),

        struct_tuple_pattern: $ => seq(
            "struct",
            "(",
            $._tuple_pattern_item,
            repeat(seq(",", $._tuple_pattern_item)),
            ")",
        ),

        // prec.dynamic prefers a new field over extending the previous pattern by constructor application.
        named_field_pattern: $ => seq(
            field('constructor', $.long_identifier),
            "(",
            choice(
                seq(
                    $._paren_field_open,
                    $.named_field_pat,
                    repeat(seq(choice(";", $._bracket_semi), $.named_field_pat)),
                    optional(choice(";", $._bracket_semi)),
                    $._bracket_close,
                ),
                seq(
                    $.named_field_pat,
                    repeat(prec.dynamic(2, seq(";", $.named_field_pat))),
                    optional(";"),
                ),
            ),
            ")",
        ),

        named_field_pat: $ => seq(
            field('name', $.identifier),
            "=",
            field('value', $.pattern),
        ),

        // Pattern precedence: `as` (0) < `|` and `&` (1) < `::` (2).
        or_pattern: $ => prec.left(1, seq($.pattern, "|", $.pattern)),

        and_pattern: $ => prec.left(1, seq($.pattern, "&", $.pattern)),

        typed_pattern: $ => seq("(", $.pattern, ":", $.type_expression, repeat(seq(choice("&", "|"), $.pattern)), ")"),

        record_pattern: $ => seq(
            "{",
            indentedOrInlineFieldList($, $.record_field_pattern, 2, {}),
            "}",
        ),

        record_field_pattern: $ => seq(
            field('name', $.long_identifier),
            "=",
            // `{ Setup = a, b }`: an unparenthesised tuple as the value.
            field('value', choice(
                $.pattern,
                $.list_tuple_pattern,
                seq($.tuple_typed_pattern, repeat(seq(",", choice($.pattern, $.tuple_typed_pattern)))),
            )),
        ),

        // No function_type here so `->` stays the match-arm separator; use `:? (int -> string)`.
        type_check_pattern: $ => prec.right(TYPE_PREC.POSTFIX + 1, seq(
            ":?",
            $._atomic_type,
            optional(seq("as", choice($.identifier, alias(prec.right(1, seq($.long_identifier, repeat1($._tuple_elem_pattern))), $.identifier_pattern)))),
        )),

        cons_pattern: $ => prec.right(2, seq($.pattern, "::", $.pattern)),

        cons_typed_head_pattern: $ => prec.right(2, seq(
            field('pattern', choice($.identifier, $.wildcard_pattern)),
            ":",
            field('type', $.type_expression),
            "::",
            $.pattern,
        )),

        wildcard_pattern: _ => "_",

        literal_pattern: $ => choice(
            $.int_literal,
            $.float_literal,
            $.negative_literal,
            $.char_literal,
            $.string_literal,
            $.verbatim_string,
            $.triple_quoted_string,
            $.bool_literal,
            $.unit,
            $.null_literal,
        ),

        negative_literal: $ => seq("-", choice($.int_literal, $.float_literal)),

        // Arguments are atomic `_tuple_elem_pattern`s so `Contains [a] [b]` is two sibling args.
        identifier_pattern: $ => choice(
            $.long_identifier,
            prec.right(1, seq($.long_identifier, repeat1($._tuple_elem_pattern))),
        ),

        // The first element is a plain pattern so `(Some x)` / `(x: T)` stay tuple_pattern / typed_pattern.
        tuple_pattern: $ => seq(
            "(",
            repeat($.attribute),
            optional("?"),
            $.pattern,
            repeat(seq(",", $._tuple_pattern_item)),
            ")",
        ),

        // Reachable only from `pattern`, never from `parameter`, so `tuple_params` owns that shape there.
        // The required comma keeps a lone `(x: T)` a typed_pattern.
        tuple_typed_first_pattern: $ => seq(
            "(",
            $.tuple_typed_pattern,
            repeat1(seq(",", $._tuple_pattern_item)),
            ")",
        ),

        _tuple_pattern_item: $ => seq(
            repeat($.attribute),
            optional("?"),
            choice(
                $.pattern,
                $.tuple_typed_pattern,
            ),
        ),

        // `name : type` inside a parenthesised tuple; the constraint clause covers
        // `((x: 'M when 'M : (static member (>>=) : ...), f), _mthd)`.
        tuple_typed_pattern: $ => seq(
            // `(g2, s2): Lens<'a,'b>`: a parenthesised tuple may carry the ascription as one element.
            field('pattern', choice(
                $.long_identifier, $.wildcard_pattern, $.tuple_pattern,
                alias(seq($.long_identifier, $.tuple_pattern), $.identifier_pattern),   // `(f, NonEmptyList(h, t): NonEmptyList<'a>)`
            )),
            ":",
            field('type', $.type_expression),
            optional($._when_constraints),
        ),

        // Excludes identifier_pattern's constructor-application form: it would consume
        // `add a b` in `let add a b = ...` before the parser sees there is no `,`.
        _tuple_elem_pattern: $ => choice(
            seq("(", $.operator_name, repeat1($._simple_expression), ")"),   // `| NLambdas ((-) n 1) (vs, b) ->`
            $.long_identifier,
            $.wildcard_pattern,
            $.literal_pattern,
            $.tuple_pattern,
            $.tuple_typed_first_pattern,
            $.struct_tuple_pattern,
            $.typed_pattern,
            $.record_pattern,
            $.list_pattern,
            $.array_pattern,
            // In `let a, b as c, d = ...` F# binds `as` tighter than `,` (a tuple element is a headBindingPattern).
            alias($.as_tuple_elem_pattern, $.as_pattern),
        ),

        // `let Ctor(a, b), rest = ...`. `_ctor_tuple_gate` fires only when `ident ( ... ) ,` follows;
        // a function definition never has `,` after its params, so its LR path is untouched.
        ctor_first_tuple_pattern: $ => seq(
            $._ctor_tuple_gate,
            $.long_identifier,
            // The gate only fires on bare identifier args (`AesKey key, ...`).
            choice($.tuple_pattern, repeat1(choice($.long_identifier, $.wildcard_pattern))),
            optional(seq("as", $.identifier)),
            ",",
            $._tuple_elem_or_ctor,
            repeat(seq(",", $._tuple_elem_or_ctor)),
        ),

        as_tuple_elem_pattern: $ => prec.right(1, seq(
            choice($.long_identifier, $.wildcard_pattern, $.tuple_pattern,
                $.record_pattern, $.list_pattern, $.array_pattern),
            "as",
            $.identifier,
        )),

        // Not in `pattern`: match arms handle commas via their own repeat.
        unparenthesized_tuple_pattern: $ => seq(
            $._tuple_elem_pattern,   // a leading access modifier belongs to the let signature
            ",",
            $._tuple_elem_or_ctor,
            repeat(seq(",", $._tuple_elem_or_ctor)),
        ),

        _tuple_elem_or_ctor: $ => seq(
            optional($.access_modifier),
            choice(
                $._tuple_elem_pattern,
                alias(prec.right(1, seq($.long_identifier, repeat1($._tuple_elem_pattern))), $.identifier_pattern),
            ),
        ),

        as_pattern: $ => prec.right(seq(
            $.pattern,
            "as",
            choice($.identifier, $.tuple_pattern),
        )),

        // `[ a, b, c ]` is a one-element list holding a tuple.
        list_tuple_pattern: $ => prec.right(seq($.pattern, repeat1(seq(",", $.pattern)))),
        // `| [arg: Expr] ->`: the brackets stand in for typed_pattern's parens.
        _list_pattern_item: $ => choice($.pattern, $.list_tuple_pattern, $.tuple_typed_pattern),

        list_pattern: $ => seq(
            "[",
            optional(choice(
                $._block_pattern_items,
                seq(
                    $._list_pattern_item,
                    repeat(seq(";", $._list_pattern_item)),
                    optional(";"),
                ),
            )),
            "]",
        ),

        array_pattern: $ => seq(
            "[|",
            optional(choice(
                $._block_pattern_items,
                seq(
                    $._list_pattern_item,
                    repeat(seq(";", $._list_pattern_item)),
                    optional(";"),
                ),
            )),
            "|]",
        ),

        // The prec(20) branches keep single-identifier parenthesised params flat instead of
        // nested in tuple_params/tuple_pattern.
        parameter: $ => choice(
            $.identifier,
            $.unit,
            $.wildcard_pattern,
            $.struct_tuple_pattern,
            // `(value: 'T when 'T: null)` / `(resource: 'T :> IDisposable)`.
            prec(20, seq("(", repeat($.attribute), $.identifier, ":", $.type_expression, optional(choice($._when_constraints, seq(":>", $.type_expression))), ")")),
            prec(20, seq("(", "(", repeat1($.attribute), $.identifier, ":", $.type_expression, ")", ")")),   // `(([<InlineIfLambda>] f: 'a -> 'b))`
            prec(20, seq("(", repeat($.attribute), $.identifier, ")")),
            // An active pattern or an operator as a parameter: `let f q (|Pat|_|)`, `let f (<) = ...`.
            $.active_pattern_name,
            prec(20, seq("(", $.active_pattern_name, ":", $.type_expression, ")")),
            $.operator_name,
            // `?loc : T` reads `T` as the return type; an annotated optional param needs parens.
            prec(20, seq("?", $.identifier)),
            $.tuple_params,
            $.destructure_parameter,
            $.tuple_pattern,
            $.record_pattern,
        ),

        destructure_parameter: $ => seq(
            "(",
            choice(
                $.tuple_pattern,
                $.struct_tuple_pattern,
                $.record_pattern,
                $.wildcard_pattern,
                // `(Url url: Url)`
                prec.right(seq($.long_identifier, repeat1($._tuple_elem_pattern))),
            ),
            ":",
            $.type_expression,
            ")",
        ),

        // prec(APP + 1): after `e :> cm` a `^` extends the measure rather than starting the infix `^`.
        measure_power_type: $ => prec(TYPE_PREC.APP + 1, seq(
            $.long_identifier,
            "^",
            choice($.int_literal, $.negative_literal),
        )),

        measure_expression: $ => choice(
            prec.left(1, seq($.measure_expression, "/", $.measure_expression)),
            prec.left(2, seq($.measure_expression, "*", $.measure_expression)),
            // Juxtaposition `m s^-2` is a product (spec 9.5) and binds tightest.
            prec.left(3, seq($.measure_expression, $.measure_expression)),
            $.measure_power_type,
            $.int_literal,
            $.type_parameter,
            $.long_identifier,
        ),

        // `token.immediate("<")` distinguishes `3.0<cm>` from a comparison.
        measure_literal: $ => seq(
            choice($.int_literal, $.float_literal),
            token.immediate("<"),
            $.measure_expression,
            ">",
        ),

        type_expression: $ => choice(
            $.nullable_type,
            $.function_type,
            $.tuple_type,
            $.struct_tuple_type,
            $._atomic_type,
            $.anonymous_record_type,
            $.struct_anonymous_record_type,
            $.measure_power_type,
            $.flexible_type,
            // `#A & #B` (F# 7+ interface intersection).
            $.type_intersection,
            $.labelled_type,
        ),

        // `#A -> B` is `(#A) -> B`: the operand is an atomic head so `#` does not swallow `->`/`*`.
        flexible_type: $ => prec(TYPE_PREC.APP, seq(
            "#",
            $._atomic_type,
        )),

        type_intersection: $ => prec.left(TYPE_PREC.TUPLE, seq(
            $.flexible_type,
            repeat1(seq("&", $.flexible_type)),
        )),

        struct_tuple_type: $ => seq("struct", "(", $.tuple_type, ")"),

        // The return may be a bare nullable `int -> string | null`; a nullable on the left needs parens.
        function_type: $ => prec.right(TYPE_PREC.FUNCTION, seq(
            $.type_expression, "->", $.type_expression,
        )),

        tuple_type: $ => prec.right(TYPE_PREC.TUPLE, seq(
            $.type_expression,
            "*",
            $.type_expression,
            repeat(seq("*", $.type_expression)),
        )),

        // Type heads that never contain a bare `*`, `->` or `| null`.
        _atomic_type: $ => choice(
            $.generic_type,
            $.postfix_type,
            $.array_type,
            $.parenthesized_type,
            $.type_parameter,
            $.long_identifier,
        ),

        postfix_type: $ => prec.left(TYPE_PREC.POSTFIX, seq(
            $.type_expression,
            $.long_identifier,
        )),

        // The trailing `.Name` is a nested type of a generic instantiation: `ImmutableArray<'T>.Builder`.
        generic_type: $ => prec(TYPE_PREC.APP, seq(
            $.long_identifier,
            "<",
            $._generic_type_arg,
            repeat(seq(",", $._generic_type_arg)),
            ">",
            optional(seq(".", $.long_identifier)),
        )),

        _generic_type_arg: $ => choice(
            prec.dynamic(1, $.type_expression),
            $.measure_expression,
            $.static_type_argument,
            // `seq<'U :> seq<'T>>`: inline subtype constraint on a typar.
            alias($.subtype_type_arg, $.type_constraint),
        ),

        subtype_type_arg: $ => seq($.type_parameter, ":>", $.type_expression),

        // Type-provider static arguments. Bare ints and identifiers already parse via
        // measure_expression / type_expression, so the positional branch lists only the other literals.
        static_type_argument: $ => choice(
            $._static_arg_literal,
            seq(
                field('name', $.identifier),
                "=",
                field('value', choice(
                    $._static_arg_literal,
                    $.int_literal,
                    $.long_identifier,
                )),
            ),
        ),
        _static_arg_literal: $ => choice(
            $.float_literal,
            $.char_literal,
            $.string_literal,
            $.verbatim_string,
            $.triple_quoted_string,
            $.bool_literal,
            seq("-", choice($.int_literal, $.float_literal)),
        ),

        // In a cast tail (`box s :?> State[]`) this immediate `[` and bracket_index_expression's
        // tie on length; token prec 1 makes the array reading win the lex.
        array_type: $ => prec(TYPE_PREC.APP, seq(
            $.type_expression,
            choice("[", token.immediate(prec(1, "["))),
            repeat(","),
            "]",
        )),

        parenthesized_type: $ => seq("(", choice(
            $.type_expression,
            alias(seq($.type_parameter, ":>", $.type_expression), $.type_constraint),   // `(resource: ('R :> IDisposable))`
        ), ")"),

        // `| null` is one token, so a bare union `A | B` never sees it. Binds tighter than `*` and `->`,
        // looser than postfix: `int * string | null` is `int * (string | null)`.
        nullable_type: $ => seq(
            choice($._atomic_type, $.flexible_type),
            alias(token(seq("|", /[ \t]*/, "null")), "null"),
        ),

        // prec(-1): `'a'` lexes as both a type_parameter (`a'`) and a char_literal at equal length;
        // the char wins. `'a` alone still lexes as a type_parameter.
        type_parameter: _ => token(prec(-1, seq(
            choice("'", "^"),
            choice(
                /[a-zA-Z_][a-zA-Z0-9_']*/,
                /``[^`\n\r\t]+``/,
            ),
        ))),

        prefix_type_parameters: $ => choice(
            $.type_parameter,
            seq("(", $.type_parameter, repeat(seq(",", $.type_parameter)), ")"),
        ),

        _when_constraints: $ => prec.right(seq(
            "when",
            $.type_constraint,
            repeat(seq("and", $.type_constraint)),
        )),

        type_parameter_list: $ => seq(
            "<",
            repeat($.attribute), $.type_parameter,
            repeat(seq(",", repeat($.attribute), $.type_parameter)),
            optional(seq(",", "..")),   // `<'T, .. >`: "and any further typars"
            optional($._when_constraints),
            ">",
        ),


        type_constraint: $ => choice(
            // Bare IWSAM constraint (F# 7+): `when IParsable<'T>`, no `'T :` prefix.
            $.generic_type,
            seq($.type_parameter, ":>", $.type_expression),
            // SRTP default-resolution constraint: `and default ^Value : float`.
            seq("default", $.type_parameter, ":", $.type_expression),
            seq($.type_parameter, ":", "null"),
            seq($.type_parameter, ":", "not", "null"),   // F# 9 non-null constraint
            seq($.type_parameter, ":", "struct"),
            seq($.type_parameter, ":", "not", "struct"),
            seq($.type_parameter, ":", "comparison"),
            seq($.type_parameter, ":", "equality"),
            seq($.type_parameter, ":", "unmanaged"),
            seq($.type_parameter, ":", "enum", "<", $.type_expression, ">"),
            seq($.type_parameter, ":", "delegate", "<", $.type_expression, ",", $.type_expression, ">"),
            seq($.type_parameter, ":", $._srtp_member_sig),
            // `(^a or ^b) : (static member fmap: ...)`
            seq(
                "(",
                $._srtp_term,
                repeat1(seq("or", $._srtp_term)),
                ")",
                ":",
                $._srtp_member_sig,
            ),
        ),

        // A single token so the lexer never splits `(|` into `(` + `|`.
        active_pattern_name: _ => token(seq(
            "(|",
            choice(/[\p{L}_][\p{L}\p{Nd}_']*/, /``[^`\n\r\t]+``/),
            repeat(seq("|", choice(/[\p{L}_][\p{L}\p{Nd}_']*/, /``[^`\n\r\t]+``/))),
            optional(seq(/[ \t]*/, "|", /[ \t]*/, "_", /[ \t]*/)),   // `(|``Alpha Beta`` |_|)`
            "|)",
        )),

        // Class-body-only declarations are not here: they parse only as `_class_body_member`
        // children, which is what expand-selection and the indent rules rely on.
        _token: $ => choice(
            $.shebang,

            $.preproc_directive,

            $.namespace_decl,
            $.module_decl,
            $.import_decl,

            $.type_decl,
            $.type_extension,
            $.exception_decl,
            $.extern_decl,

            $.use_binding,
            $.val_field,           // signature-file `val f: int -> int`

            $._decl_or_comment,

            // prec(PREC.SEQ_EXPR) matches sequence_expression so the `expr ;` state stays a
            // GLR fork instead of being statically resolved toward the sequence shift.
            prec(PREC.SEQ_EXPR, $._expression),

            // A bare ascription as a statement: `failwith "foo" : int`;
            // `type_ascription_expression` is otherwise reachable only as a binding body.
            prec(PREC.SEQ_EXPR, $.type_ascription_expression),

            // FSI / script trailing `;` after a declaration (`open System;`); `;;` is an extra.
            ";",

            // A `///` doc with no attachment slot is its own statement instead of an ERROR.
            // prec.dynamic(-1): when an attachment reading also survives, attachment wins.
            prec.dynamic(-1, $.xml_doc_comment),
        ),

        // An F# identifier starts with a Unicode letter or `_`, then also takes digits and `'`.
        // The `` `...` `` form ends at the next double backtick, so single backticks may sit inside.
        identifier: _ => token(choice(
            /[\p{L}\p{Nl}_][\p{L}\p{Nl}\p{Mn}\p{Mc}\p{Nd}\p{Pc}\p{Cf}_']*/,
            /``([^`\n\r\t]|`[^`\n\r\t])+``/,
        )),

        // 1-2 segments stay one `long_identifier`; 3+ nest as `dot_expression(long_identifier(a,b),c)`.
        // Queries must handle both shapes - see LIMITATIONS.md.
        long_identifier: $ =>
            prec.right(PREC.DOT,
                seq(
                    $.identifier,
                    repeat(seq(".", $.identifier)),
                ),
            ),

        _int: _ => token(/[0-9][0-9_]*/),
        _hex: _ => token(seq(choice("0x", "0X"), /[0-9a-fA-F_]+/)),
        _oct: _ => token(seq(choice("0o", "0O"), /[0-7_]+/)),
        _bin: _ => token(seq(choice("0b", "0B"), /[01_]+/)),

        // `m`/`M`/`f`/`F` are integer suffixes too: `3f` and `42m` are dot-less float / decimal
        // literals. `lf`/`LF` come first so longest-match beats the single-char `l`/`L`.
        _int_suffix: _ => token.immediate(choice("lf", "LF", "uy", "us", "uL", "UL", "Ul", "ul", "un", "u", "y", "s", "l", "L", "n", "I", "m", "M", "f", "F", "Q", "R", "Z", "N", "G")),   // user-defined literal suffixes
        _float_suffix: _ => token.immediate(choice("f", "F", "m", "M")),

        int_literal: $ => seq(
            choice($._hex, $._oct, $._bin, $._int),
            optional($._int_suffix),
        ),

        // `1..10` must lex as `int .. int`, not `float(1.) . int(10)`: the dot-then-exponent
        // form (`180.e5`) requires an `[eE]` after the dot, which `..` never has.
        float_literal: $ => seq(
            choice(
                token(seq(/[0-9][0-9_]*/, ".", /[0-9][0-9_]*/, optional(seq(/[eE]/, optional(/[+-]/), /[0-9]+/)))),
                token(seq(/[0-9][0-9_]*/, ".", /[eE]/, optional(/[+-]/), /[0-9]+/)),
                token(seq(/[0-9]+/, /[eE]/, optional(/[+-]/), /[0-9]+/)),
                $._float_trailing_dot,
            ),
            optional($._float_suffix),
        ),

        // `'F'B` is a `byte` literal; the suffix is an immediate token.
        char_literal: $ => seq($._char_content, optional($._char_byte_suffix)),

        _char_content: _ => token(
            seq(
                "'",
                choice(
                    /[^'\\]/,
                    "'",            // `'''` - an unescaped single-quote char literal
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

        _char_byte_suffix: _ => token.immediate("B"),

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
                        // Line continuation: a `\` at end of line elides the
                        // newline and the next line's leading whitespace.
                        /\r?\n[ \t]*/,
                        // F# rejects unknown escape sequences (FS1157); accepted here so one bad
                        // escape does not cascade an ERROR through the rest of the file.
                        /./,
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

        // `_interp_string_text` / `_interp_verbatim_text` / `_interp_triple_text` are external
        // tokens so a leading `//` in the text is not preempted by the `line_comment` extra.

        // Body of `:fmt` inside `{expr:fmt}`, up to the closing `}`.
        _interp_format_spec: _ => token.immediate(/[^}]+/),

        // Literal `%` that isn't the start of a valid printf format spec.
        _interp_percent: _ => token.immediate('%'),

        // Printf-style format prefix `%[flags][width][.precision]conv{`. The trailing
        // `{` is required so `100% done` (no following `{`) doesn't match.
        _printf_format: _ => token.immediate(/%[-+ #0]*[0-9]*(?:\.[0-9]+)?[A-Za-z]\{/),

        _string_byte_suffix: _ => token.immediate("B"),

        string_literal: $ => seq($._string_content, optional($._string_byte_suffix)),

        verbatim_string: $ => seq($._verbatim_string_content, optional($._string_byte_suffix)),

        // F# has no byte suffix on triple-quoted strings.
        triple_quoted_string: _ => token(
            seq(
                '"""',
                repeat(choice(/[^"]+/, /"[^"]/, /""[^"]/)),
                '"""',
            )
        ),

        interpolation: $ => choice(
            // `_printf_format` includes the opening `{`.
            seq(
                alias($._printf_format, $.printf_format_string),
                $._expression,
                '}',
            ),
            seq(
                '{',
                $._expression,
                optional(seq(':', alias($._interp_format_spec, $.format_string))),
                '}',
            ),
        ),

        interpolated_string: $ => seq(
            '$"',
            repeat(choice(
                $.interpolation,
                alias($._interp_string_text, $.string_content),
                alias($._interp_percent, $.string_content),
            )),
            '"',
        ),

        interpolated_verbatim_string: $ => seq(
            choice('$@"', '@$"'),
            repeat(choice(
                $.interpolation,
                alias($._interp_verbatim_text, $.string_content),
                alias($._interp_percent, $.string_content),
            )),
            '"',
        ),

        // F# 8 extended interpolation: with N dollars the hole delimiters are N braces and
        // single braces are text, so the whole literal is one opaque token.
        multidollar_string: _ => token(seq(/\$\$+/, '"""', /([^"]|"[^"]|""[^"])*/, '"""')),

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

        // F# reads `(` and `)` separated by whitespace, newlines or a block comment as the unit
        // literal, so that filler is part of the token instead of being absorbed as `extras`.
        unit: _ => token(seq("(", repeat(choice(/\s/, /\(\*([^*]|\*+[^)*])*\*+\)/)), ")")),

        // `^1` in `arr[^1]`. Digits only: a bare `^` prefix would out-lex the `^a` type
        // parameter. Lexical prec -1 so the measure power `^` wins where it is valid.
        from_end_index: _ => token(prec(-1, seq("^", /[0-9][0-9_]*/))),

        null_literal: _ => token("null"),

        // token prec 1: at equal length `//&&` must lex as a comment, not as a `symbolic_op`.
        // `[^/]` must exclude the newline: a `[^x]` class matches `\n` and eats the next line.
        line_comment: _ => token(prec(1, seq("//", choice(/[^/\n\r].*/, "")))),

        // prec 2 > line_comment's 1: lexical prec overrides match length.
        xml_doc_comment: _ => token(prec(2, seq("///", /.*/))),

        // Also external tokens: the scanner handles nested comments, these regexes lex the
        // non-nested cases. The char after `(*` must not be `)`: `(*)` is the multiply operator.
        block_comment: _ => token(seq("(*", choice(/\*+\)/, seq(/[^)*]|\*+[^)*]/, /([^*]|\*+[^)*])*\*+/, ")")))),
        block_doc_comment: _ => token(prec(1, seq("(**", /([^*]|\*+[^)*])*\*+/, ")"))),


        // Non-structural directives: `#nowarn`, `#r`, `#load`, `#line`, ... Structural
        // directives `#if/#elif/#else/#endif` use dedicated higher-priority tokens.
        preproc_keyword: _ => token(seq("#", /[ \t]*/, /[a-zA-Z_][a-zA-Z0-9_]*/, /[ \t]*/)),

        // `# 14 "pars.fs"` / `#line 14 "f"` - fsyacc/fslex line directives.
        // Trivia: an extra token, skipped by the scanner's geometry too.
        line_directive: _ => token(seq("#", /[ \t]*/, /[0-9]+/, optional(seq(/[ \t]+/, '"', /[^"\n]*/, '"')))),

        preproc_directive: $ => prec.right(seq(
            field('name', $.preproc_keyword),
            // Only literals, never a `long_identifier`: whitespace and newlines are `extras`, so
            // any looser argument grabs the next line's first token. Not a repeat, for the same reason.
            optional(field('argument', $.int_literal)),
            optional(field('argument', choice($.string_literal, $.verbatim_string))),
        )),

        // `#!` cannot collide with `preproc_keyword`, which requires an identifier after `#`.
        shebang: _ => token(seq("#!", /[^\n\r]*/)),

        // Structural directives - prec(1) > preproc_keyword's prec 0 when both match
        // the same string. Longer matches still win, so `#ifdef` falls to preproc_keyword.
        preproc_if_kw: _ => token(prec(1, seq("#if", /[ \t]*/))),
        preproc_elif_kw: _ => token(prec(1, seq("#elif", /[ \t]*/))),
        preproc_else_kw: _ => token(prec(1, seq("#else", /[ \t]*/))),
        preproc_endif_kw: _ => token(prec(1, seq("#endif", /[ \t]*/))),

        // `preproc_if` is an `extra` and extras must have an unambiguous ending, so the whole
        // condition is one atomic token and cannot be sub-coloured.
        preproc_expression: _ => token(/[^\n\r]+/),

        // Body-less nodes in `extras`, so a directive may sit in any context; the guarded code is
        // parsed in place, with the `#if` and `#else` branches as siblings.
        preproc_if: $ => seq($.preproc_if_kw, field('condition', $.preproc_expression)),
        preproc_elif: $ => seq($.preproc_elif_kw, field('condition', $.preproc_expression)),
    }
});
