/**
 * @file F# tree sitter definition focused on Helix
 * @author Mangel Maxime
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

// Type-level precedences (only used inside type expressions). Strict total
// order — each tier wraps the next.
const TYPE_PREC = {
    FUNCTION: 1,   // int -> string             (right-assoc, lowest)
    TUPLE:    2,   // int * string
    POSTFIX:  3,   // int list, int option      (left-assoc)
    APP:      4,   // list<int>, int[]          (highest)
};

// Expression-level precedences. Values are listed low-to-high; equalities
// between names at the same value are deliberate and documented below.
const PREC = {
    // Tier 1 — body-greedy / lowest-binding. Anything here is intentionally
    // weaker than the operators below so that bodies expand to consume as
    // much of the trailing expression chain as possible.
    SEQ_EXPR:       1,   // virtual-semi sequence  (must be loosest)
    PIPE_EXPR:      1,   // |>, <|, >>, <<
    FUN_EXPR:       1,   // `fun x -> …` — low so `->` body expands greedily
    LET_EXPR:       1,   // `let x = … in …`     — same reason
    TUPLE_EXPR:     1,   // below BOOL_OR so `||` binds tighter than `,`

    BOOL_OR:        2,   // ||
    RARROW:         3,   // dispatch wrapper for _expression
    BOOL_AND:       3,   // &&
    INFIX_OP:       4,   // = <> < > <= >= :: and custom symbolic
    ADDITIVE:       5,   // + -
    MULTIPLICATIVE: 6,   // * / %

    // Tier 7+ — declaration / control flow / application binding.
    LET_DECL:       7,   // `let f x = …` (top-level binding)
    MATCH_EXPR:     8,   // match / try-with / function
    IF_EXPR:        14,  // if / elif / else / for / while
    PREFIX_EXPR:    15,  // unary `not` / `~~~` / `-` / `&` / `lazy` / `assert`
    CE_EXPR:        15,  // `builder { … }` — below APP_EXPR so `f { … }` is application
    APP_EXPR:       16,  // `f x` — application binds tighter than CE / prefix
    LARROW:         16,  // `expr <- expr`  (mutation, similar binding)

    // Tier 19+ — atomic / postfix access. Tightest binding.
    DOT:            19,  // `a.b.c` long_identifier chain
    INDEX_EXPR:     20,  // `arr.[i]`
    PAREN_EXPR:     21,  // `(expr)` / `begin … end` / quotations
    TYPED_EXPR:     22,  // `(expr : ty)`
    DOTDOT_SLICE:   23,  // `expr..` and `..expr` inside index args
    NEW_OBJ:        24,  // `new T(…)`
};

export default grammar({
    name: "fsharp",

    word: $ => $.identifier,

    // Reserved word sets. `global` is intentionally empty — every keyword the
    // grammar uses is a string literal in some rule, and the parser only ever
    // accepts those positions. Since this is a syntax-highlighter-focused
    // grammar, accepting nonsense like `let else = 1` (which the F# compiler
    // rejects) is a fine trade-off for not maintaining an enumeration of every
    // F# keyword.
    //
    // `query_ce` is activated only inside `computation_expression` bodies (via
    // `reserved('query_ce', …)`) so query custom operators like `where`/`select`
    // become their own tokens there while staying plain identifiers everywhere
    // else (e.g. `List.where`, `let take n = …`).
    reserved: {
        global: _ => [],
        query_ce: _ => [
            'select', 'where', 'sortBy', 'sortByDescending',
            'thenBy', 'thenByDescending', 'take', 'skip',
            'takeWhile', 'skipWhile', 'distinct', 'count',
            'head', 'last', 'exactlyOne',
            'minBy', 'maxBy', 'sumBy', 'averageBy',
            'find', 'exists', 'all', 'contains', 'nth',
            'headOrDefault', 'lastOrDefault', 'exactlyOneOrDefault',
            'groupBy', 'groupValBy', 'groupJoin',
            'join', 'leftOuterJoin', 'on', 'into',
        ],
    },

    // Externals are zero-width tokens emitted by src/scanner.c. See that file for
    // details of the offside-rule scanner state.
    externals: $ => [
        $._body_indent,    // delimits let_binding bodies
        $._body_dedent,
        $._indent,         // delimits indented let_decl_indented bodies
        $._dedent,
        $._inline_open,    // delimits same-line let_decl_indented bodies
        $._inline_close,
        $._virtual_semi,        // virtual semicolon between sibling expressions on
                           // separate lines (F# implicit sequence operator)
    ],

    extras: $ => [/\s+/, $.xml_doc_comment, $.line_comment, $.block_comment, $.block_doc_comment],

    // Conflict declarations enable GLR exploration where LALR(1) is insufficient.
    // Without them, prec.dynamic is silently ignored.
    conflicts: $ => [
        // After a value expression, a bare identifier could extend it (postfix_type /
        // application_expression argument) or name the next record field.
        [$.record_type_field, $.postfix_type],
        // After `name: T` in a named union field, `*` could start the next field or
        // extend T into a tuple_type.
        [$._union_field_type, $.type_expression],
        // A single long_identifier could be either a measure or a type expression
        // inside generic args or aliases.
        [$.measure_expression, $.type_expression],
        // After `type Foo`, the following `=` chooses type_decl, `with` chooses type_extension.
        [$.type_decl, $.type_extension_name],
        // After `module M =`, the identifier is either a module abbreviation target
        // or the first declaration of a nested module body.
        [$.module_decl],
        // Attribute / doc-comment prefix: at top level the same `[<…>]` or `///`
        // token could be a standalone `_token` child OR the start of a
        // decl's decoration prefix. GLR explores both; we bias toward
        // attachment via `prec.dynamic` on the decl branch.
        [$._token, $.let_binding, $.module_decl, $.exception_decl],
        // Same situation inside a class/type body — `[<…>]` or `///` could be
        // a standalone `_class_body_member` or the start of any decoratable
        // member's prefix.
        [$._class_body_member, $.member_defn, $.let_binding, $.abstract_member_defn, $.secondary_constructor],
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
        // module [private|internal] Foo =   (nested module — body indented under,
        //                                    consumed as children via _body_indent)
        // module M = Lib                    (module abbreviation — target captured as abbrev field)
        // module M = Lib.Math.Integer       (qualified abbreviation target)
        //
        // After `=` we choose between an abbreviation target (inline
        // long_identifier) and an indented body (declarations as children).
        module_decl: $ => prec.dynamic(1, seq(
            repeat(choice($.attribute, $.xml_doc_comment, $.block_doc_comment)),
            "module",
            optional($.access_modifier),
            optional("rec"),
            field('name', $.long_identifier),
            optional(seq("=", optional(choice(
                field('abbrev', $.long_identifier),
                seq(
                    $._body_indent,
                    repeat($._token),
                    $._body_dedent,
                ),
            )))),
        )),

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


        // Body of a `type ... =` or `and ... =` declaration. Shared by type_decl and
        // type_and_decl. Class/interface bodies aren't included here — their members
        // appear as flat top-level _token siblings after the type header.
        //   type Point = { X: int; Y: int }
        //   type Shape = | Circle of float | Rectangle of float * float
        //   type Foo(x: int) =
        //   type IFoo =
        _type_decl_body: $ => choice(
            $.record_type_defn,
            $.union_type_defn,
            $.enum_type_defn,
            $.delegate_type_defn,
            $.struct_type_defn,
            $.class_type_defn,
            $.interface_type_defn,
            field('alias', $.measure_expression),
            prec.dynamic(1, field('alias', $.type_expression)),
        ),

        // `=` is optional: `[<Measure>] type kg` and empty class/interface bodies have none.
        // After `=`, the body takes one of two shapes:
        //   • Inline (same line): a `_type_decl_body` — record `{…}`, union/enum
        //     `| Case`, alias, etc. — parses directly without any body-indent token.
        //   • Indented (own line at deeper column): `_body_indent` fires, and the
        //     content inside is EITHER the same `_type_decl_body` (for record /
        //     union / enum / etc. wrapping at the type-body column) OR a sequence
        //     of class-body members (`member this.X = …`, `val`, etc.).
        //
        // This makes class/extension members CHILDREN of `type_decl` rather than
        // `_token` siblings — fixes expand-selection (member → type → file) and
        // gives "Enter after a member" the correct indent (the member's own column).
        type_decl: $ => prec.right(seq(
            "type",
            repeat($.attribute),
            field('name', $.identifier),
            optional($.type_parameter_list),
            optional($.primary_constructor),
            optional(seq("=", optional($._type_decl_body_or_class))),
            repeat($.type_and_decl),
        )),

        // and Even = ...  (mutual type recursion continuation)
        type_and_decl: $ => prec.right(seq(
            "and",
            repeat($.attribute),
            field('name', $.identifier),
            optional($.type_parameter_list),
            optional($.primary_constructor),
            optional(seq("=", optional($._type_decl_body_or_class))),
        )),

        // Intrinsic or external type extension. The body — extension members —
        // follows the `with` and is wrapped by `_body_indent`/`_body_dedent` so
        // members are children of `type_extension`, not siblings.
        //   type Foo with             type Foo<'T> with             type System.String with
        type_extension: $ => seq(
            "type",
            repeat($.attribute),
            field('name', $.type_extension_name),
            optional($.type_parameter_list),
            "with",
            optional(seq(
                $._body_indent,
                repeat($._class_body_member),
                $._body_dedent,
            )),
        ),

        // Body of `type Foo = …` (or `and Foo = …`): inline `_type_decl_body`
        // when on the same line as `=`, or an indented wrap when on a new line.
        // Inside the indented wrap, the body content is EITHER another
        // `_type_decl_body` (for record/union/enum/alias whose first significant
        // token sits at the body column) or a sequence of class-body members.
        _type_decl_body_or_class: $ => choice(
            $._type_decl_body,
            seq(
                $._body_indent,
                choice(
                    $._type_decl_body,
                    repeat1($._class_body_member),
                ),
                $._body_dedent,
            ),
        ),

        // Everything legal inside a class or type-extension body.
        _class_body_member: $ => choice(
            $.attribute,
            $.inherit_decl,
            $.member_defn,
            $.abstract_member_defn,
            $.interface_impl,
            $.secondary_constructor,
            $.val_field,
            $.do_stmt,
            $.let_binding,
            $.xml_doc_comment,
            $.line_comment,
            $.block_comment,
            $.block_doc_comment,
        ),

        type_extension_name: $ => choice(
            seq($.identifier, repeat1(seq(".", $.identifier))),  // qualified: System.String
            $.identifier,                                          // simple: Foo
        ),

        // Parenthesised comma-separated parameter group — OOP/tuple calling convention.
        // Used in primary constructors, secondary constructors, and method members.
        // Each element may be optional (?name) and/or typed (name: type).
        tuple_params: $ => seq(
            "(",
            optional(seq(
                $.tuple_param,
                repeat(seq(",", $.tuple_param)),
            )),
            ")",
        ),

        // Primary constructor for class types: `type T()` or `type Dog(name: string, …)`.
        // The `unit` branch handles `type T()` — the lexer atomises `()` into the unit
        // token, so we accept it here rather than splitting it back into `(` `)`.
        primary_constructor: $ => prec(20, choice(
            $.unit,
            seq(
                "(",
                $.tuple_param,
                repeat(seq(",", $.tuple_param)),
                ")",
            ),
        )),

        tuple_param: $ => seq(
            repeat($.attribute),    // [<DefaultParameterValue(42)>], [<CallerMemberName>], etc.
            optional("?"),
            $.identifier,
            optional(seq(":", $.type_expression)),
        ),

        // Secondary class constructor: `new(args) = expr [then expr]`.
        // Distinct from new_expression: that one requires a type name between `new` and `(`.
        secondary_constructor: $ => prec.right(prec.dynamic(1, seq(
            repeat(choice($.attribute, $.xml_doc_comment, $.block_doc_comment)),
            optional($.access_modifier),
            "new",
            field('parameters', $.tuple_params),
            "=",
            field('body', $._expression),
            optional(seq("then", $._expression)),
        ))),

        // `: TypeExpr` return-type annotation. Shared by let_binding, let_and_binding,
        // let_decl_indented, let_expression Branch B, _method_body, and auto-properties.
        _return_type_annot: $ => seq(":", field('return_type', $.type_expression)),

        // `member/override/default [inline] self.Name` — shared by method and property forms.
        _instance_member_prefix: $ => seq(
            choice("member", "override", "default"),
            optional("inline"),
            field('self', $.member_self_ident),
            ".",
            field('name', $.identifier),
        ),

        // `static [inline] member Name` — shared by method and property forms.
        _static_member_prefix: $ => seq(
            "static",
            optional("inline"),
            "member",
            field('name', $.identifier),
        ),

        // `params [:return-type] = expr` — shared by instance and static method members.
        // Body optional so mid-edit `member this.Foo() =` still produces a real
        // `member_defn` node (no MISSING-identifier recovery), giving Helix's
        // indent walk something concrete to anchor `@extend` against. prec.right
        // keeps the body greedy when it's present (parser prefers consuming the
        // expression over reducing `_method_body` at the `=`).
        _method_body: $ => prec.right(seq(
            field('parameters', repeat($.parameter)),
            optional($._return_type_annot),
            "=",
            optional(field('body', $._expression)),
        )),

        // `with get/set accessor [and get/set accessor]` — shared by property forms.
        _accessor_body: $ => seq(
            "with",
            $.property_accessor,
            optional(seq("and", $.property_accessor)),
        ),

        // Method / property / auto-property forms:
        //   member this.Name = expr
        //   member this.Method arg : RetType = expr
        //   static member Name args = expr
        //   override this.ToString() = expr
        //   member this.Prop with get() = e [and set(v) = e]
        //   [static] member val AutoProp = expr [with get [, set]]
        // `prec.dynamic(1, …)` on every branch biases toward attaching leading
        // `[<…>]` and `///` to the member rather than leaving them as
        // standalone `_class_body_member` siblings (which is the competing
        // reading at the choice point).
        member_defn: $ => choice(
            prec.dynamic(1, seq(
                repeat(choice($.attribute, $.xml_doc_comment, $.block_doc_comment)),
                $._instance_member_prefix, $._method_body,
            )),
            prec.dynamic(1, seq(
                repeat(choice($.attribute, $.xml_doc_comment, $.block_doc_comment)),
                $._static_member_prefix, $._method_body,
            )),
            prec.dynamic(1, seq(
                repeat(choice($.attribute, $.xml_doc_comment, $.block_doc_comment)),
                $._instance_member_prefix, $._accessor_body,
            )),
            prec.dynamic(1, seq(
                repeat(choice($.attribute, $.xml_doc_comment, $.block_doc_comment)),
                $._static_member_prefix, $._accessor_body,
            )),
            // Auto-property — instance/static differ only by the `static` prefix.
            prec.dynamic(1, seq(
                repeat(choice($.attribute, $.xml_doc_comment, $.block_doc_comment)),
                optional("static"),
                "member",
                "val",
                field('name', $.identifier),
                optional($._return_type_annot),
                "=",
                $._expression,
                optional($.auto_property_accessors),
            )),
        ),

        // get() = expr  or  set(v) = expr  (inside a property definition)
        property_accessor: $ => seq(
            choice("get", "set"),
            field('parameters', repeat($.parameter)),
            "=",
            field('body', $._expression),
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
        abstract_member_defn: $ => prec.dynamic(1, seq(
            repeat(choice($.attribute, $.xml_doc_comment, $.block_doc_comment)),
            optional("static"),
            "abstract",
            optional("member"),
            field('name', $.identifier),
            ":",
            $.type_expression,
        )),

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

        // interface IFoo with                interface IBar with
        //     member this.A = …                  member _.B = …
        //
        // Same `_body_indent`/`_body_dedent` pattern as `type_extension`: the
        // member impls following `with` become children of the `interface_impl`
        // node, so expand-selection walks identifier → member_defn →
        // interface_impl → enclosing class/object_expression.
        interface_impl: $ => seq(
            "interface",
            field('type', $.type_expression),
            optional(seq(
                "with",
                optional(seq(
                    $._body_indent,
                    repeat($._class_body_member),
                    $._body_dedent,
                )),
            )),
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

        // `repeat($.line_comment)` absorbs trailing comments INTO `union_case`
        // so the next case's `|` becomes the parser's one-token lookahead.
        // Without it, a line comment after a case is the lookahead and the
        // parser reduces `union_type_defn` early (the action table at the
        // case's end has no shift for `line_comment` other than as extras).
        // `prec.right` resolves the shift/reduce conflict in favour of
        // absorbing the comment.
        union_case: $ => prec.right(seq(
            "|",
            repeat($.attribute),   // `| [<DefaultValue>] X` — attribute on a DU case
            field('name', $.identifier),
            optional(seq("of", choice(
                $.union_case_named_fields,
                field('fields', $.type_expression),
            ))),
            repeat($.line_comment),
        )),

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

        // Type allowed inside a named union field — excludes tuple_type so that `*`
        // between fields is never mistaken for a tuple separator inside the previous
        // field's annotation.
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

        // type Point3D = struct val x: float … end — block-style bodies hold the
        // same class-body members as `type Foo() = …` (no _body_indent needed
        // since `struct`/`class`/`interface` is the open and `end` is the close).
        struct_type_defn: $ => seq("struct", repeat($._class_body_member), "end"),

        // type Foo() = class member … end  — explicit class block.
        class_type_defn: $ => seq("class", repeat($._class_body_member), "end"),

        // type IFoo = interface abstract … end  — explicit interface block.
        // Distinguished from interface_impl (which sits in class bodies as `interface T with …`)
        // by the trailing `end`; ambiguity at the leading `interface` is explored via GLR.
        interface_type_defn: $ => seq("interface", repeat($._class_body_member), "end"),

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

        // Two body forms:
        //   Multi-line:  `{` then `_body_indent` (scanner pushes the field column)
        //                then fields separated by `_virtual_semi` (or explicit `;`)
        //                then `_body_dedent` then `}`.
        //   Inline:      `{ X = 1; Y = 2 }` — single line, explicit `;` only.
        // The indented form prevents the previous field's type from greedily
        // consuming the next field's name across a newline (e.g. `unit -> unit`
        // followed by `A : 'A` was parsed as `unit -> (unit A)` via postfix_type,
        // erroring on the trailing `:`).
        record_type_defn: $ => seq(
            "{",
            choice(
                seq(
                    $._body_indent,
                    $.record_type_field,
                    repeat(prec.dynamic(TYPE_PREC.POSTFIX + 1, seq(
                        choice(";", $._virtual_semi),
                        $.record_type_field,
                    ))),
                    optional(choice(";", $._virtual_semi)),
                    $._body_dedent,
                ),
                seq(
                    $.record_type_field,
                    repeat(prec.dynamic(TYPE_PREC.POSTFIX + 1, seq(";", $.record_type_field))),
                    optional(";"),
                ),
            ),
            "}",
        ),

        // prec(POSTFIX) on the field ties its REDUCE precedence with postfix_type's SHIFT
        // precedence, letting prec.dynamic in record_type_defn resolve the conflict.
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
            $.sequence_expression,
        )),

        // F#'s implicit sequence — multiple expressions at the same indent inside
        // a body (function, if/then/else, for, while, lambda, …). The scanner
        // emits a zero-width `_virtual_semi` between expressions when a newline
        // crosses an offside-rule boundary; GLR exploration sorts out sequences
        // vs continuations.
        sequence_expression: $ => prec.left(PREC.SEQ_EXPR, seq(
            $._expression,
            repeat1(seq($._virtual_semi, $._expression)),
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

        // Argument restricted to _simple_expression (no let/if/match/lambda/binary)
        // so that adjacent let bindings or trailing expressions aren't pulled into
        // the application.
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
            // Accept `async { … }` / `task { … }` etc. as application arguments.
            // Without this, a body that sequences a CE after another statement
            // (`printfn "a"; async { … } |> ignore`) hits a parse error because
            // `{ return … }` doesn't fit as a record_expression. Including
            // computation_expression here lets the parser fall back to a chained
            // application (semantically wrong but free of ERROR) — the proper
            // fix is sequence-expression support, tracked separately.
            $.computation_expression,
        ),

        // All infix operations in one rule (one rule keeps post-_expression state bloat down).
        // Each alternative carries its own prec to resolve shift/reduce between operators.
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

        // Two-branch form (like `if_expression`):
        //   prec(2) — body present (`fun x -> body`)
        //   prec(1) — body absent (`fun x ->`), mid-edit shape so Helix can still
        //             anchor indent on the `lambda_expression` node.
        // Body uses `_indented_or_inline_body` so multi-line lambdas pick up the
        // scanner's `_body_indent`/`_virtual_semi` machinery (sequence bodies).
        lambda_expression: $ => prec.right(PREC.FUN_EXPR,
            choice(
                prec(2, seq(
                    "fun",
                    repeat1($.parameter),
                    "->",
                    field('body', $._indented_or_inline_body),
                )),
                prec(1, seq(
                    "fun",
                    repeat1($.parameter),
                    "->",
                )),
            ),
        ),

        unary_expression: $ => prec(PREC.PREFIX_EXPR, seq(
            choice("not", "~~~", "-", "!"),
            $._expression,
        )),

        // Custom operators: `@` (list append) or any sequence of 2+ symbolic chars.
        // Restricted to 2+ chars and `@` excluded as a start char so that quotation
        // delimiters `@>` and `@@>` stay as their own tokens. Single-char operators
        // like `+`/`-`/`*` are tokens of their own elsewhere in the grammar.
        symbolic_op: _ => token(choice(
            "@",
            /[!$%&*+\-\/<=>?^|][!$%&*+\/<=>?@^|~]+/,
        )),

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

        // {| Name = "Alice"; Age = 30 |}  or  {| r with Name = "Bob" |}
        // Copy-update base is _simple_expression so it can't be confused with the
        // record_field's `name = value` form. Like `record_type_defn`, we have
        // two body forms: indented (uses `_body_indent` + `_virtual_semi` for
        // field separation) and inline (explicit `;` only). The indented form
        // prevents a field's value expression from greedily absorbing the next
        // field's name across a newline.
        anonymous_record_expression: $ => seq(
            "{|",
            choice(
                seq(
                    field('base', $._simple_expression),
                    "with",
                    $._record_fields,
                ),
                $._record_fields,
            ),
            "|}",
        ),

        // { x = 1; y = 2 } or { r with x = 1 } — see anonymous_record_expression for the
        // disambiguation rationale, which is identical.
        record_expression: $ => seq(
            "{",
            choice(
                seq(
                    field('base', $._simple_expression),
                    "with",
                    $._record_fields,
                ),
                $._record_fields,
            ),
            "}",
        ),

        // Shared field-list body for record/anonymous-record expressions.
        _record_fields: $ => choice(
            seq(
                $._body_indent,
                $.record_field,
                repeat(prec.dynamic(PREC.APP_EXPR + 1, seq(
                    choice(";", $._virtual_semi),
                    $.record_field,
                ))),
                optional(choice(";", $._virtual_semi)),
                $._body_dedent,
            ),
            seq(
                $.record_field,
                repeat(prec.dynamic(PREC.APP_EXPR + 1, seq(";", $.record_field))),
                optional(";"),
            ),
        ),

        // prec(APP_EXPR) lets prec.dynamic in record_expression/anonymous_record_expression
        // prefer starting a new field over extending the value via application_expression.
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

        // Two explicit branches (like `let_binding`):
        //   prec(2) — bodies present (`if cond then expr [elif … then expr]* [else expr]`)
        //   prec(1) — incomplete shape (`if cond then`) so mid-edit input still parses
        //             as a real `if_expression` and Helix's indent walk has a node to
        //             anchor `@extend` against.
        // The previous unified `optional(body)` form made the parser prefer the shorter
        // parse, which broke `if a then "a" else if b then "b" elif c then "c" else "x"`
        // — the inner `if b then "b" elif c then` ended early and `then "c"` was orphaned.
        // `_indented_or_inline_body`: the body of if-then/elif/else/for/while/lambda.
        // Wrapped with `_body_indent`/`_body_dedent` (scanner externals) when the body
        // sits on its own line — that pushes the body column onto the scanner's indent
        // stack, which is what `_virtual_semi` uses to recognise sibling expressions at the
        // same indent (F#'s implicit sequence operator). When the body is inline (same
        // line as `then`/`do`/`->`), no indent token fires and the body is a single
        // _expression.
        _indented_or_inline_body: $ => choice(
            seq($._body_indent, $._expression, $._body_dedent),
            $._expression,
        ),

        if_expression: $ => prec.right(PREC.IF_EXPR, choice(
            prec(2, seq(
                "if",
                $._expression,
                "then",
                $._indented_or_inline_body,
                repeat(seq("elif", $._expression, "then", $._indented_or_inline_body)),
                optional(seq("else", $._indented_or_inline_body)),
            )),
            prec(1, seq(
                "if",
                $._expression,
                "then",
            )),
        )),

        // `(>>=)` `(+)` `(|>)` — operator name wrapper. The single-char alternatives
        // (`+`, `-`, etc.) are listed explicitly because symbolic_op requires 2+ chars.
        operator_name: $ => seq(
            "(",
            choice(
                $.symbolic_op,
                "+", "-", "*", "/", "%",
                "=", "<", ">",
                "&", "|", "^",
            ),
            ")",
        ),

        // The bindable name in any let-family rule. Shared by let_binding,
        // let_and_binding, let_decl_indented, and let_expression Branch B.
        _let_name_pattern: $ => choice(
            $.identifier, $.operator_name, $.active_pattern_name,
            $.typed_pattern, $.tuple_pattern, $.struct_tuple_pattern, $.unparenthesized_tuple_pattern, $.record_pattern, $.list_pattern, $.array_pattern, $.wildcard_pattern,
        ),

        // `[inline/mutable] name [type-params] params [:return-type]` — the middle of a
        // let-family binding. Leading `static`/`rec` and trailing `= body` stay at the
        // call sites since they differ per rule.
        _let_signature: $ => seq(
            optional(choice("inline", "mutable")),
            field('name', $._let_name_pattern),
            optional($.type_parameter_list),
            field('parameters', repeat($.parameter)),
            optional($._return_type_annot),
        ),

        // Two explicit branches so the body-present case has a static `prec` win
        // over the body-absent case. `optional(body)` + GLR exploration didn't
        // bias correctly — tree-sitter ended up preferring the shorter "no body"
        // parse and turned `let x = 1` into `let_binding` + a sibling `int_literal`.
        // The body-absent branch lets mid-edit `let x =` parse as a real
        // `let_binding` node so Helix's indent walk has something to anchor on,
        // and the `!body` field-absence predicate in `indents.scm` targets it.
        let_binding: ($) => prec.right(PREC.LET_DECL, choice(
            prec(2, prec.dynamic(1, seq(
                repeat(choice($.attribute, $.xml_doc_comment, $.block_doc_comment)),
                optional("static"),
                "let",
                optional("rec"),
                $._let_signature,
                "=",
                choice(
                    seq($._body_indent, field('body', $._expression), $._body_dedent),
                    field('body', $._expression),
                ),
                repeat($.let_and_binding),
            ))),
            prec(1, prec.dynamic(1, seq(
                repeat(choice($.attribute, $.xml_doc_comment, $.block_doc_comment)),
                optional("static"),
                "let",
                optional("rec"),
                $._let_signature,
                "=",
                repeat($.let_and_binding),
            ))),
        )),

        // and name params [: type] = expr  (mutual recursion continuation)
        let_and_binding: ($) => prec.right(PREC.LET_DECL, choice(
            prec(2, seq("and", $._let_signature, "=", field('body', $._expression))),
            prec(1, seq("and", $._let_signature, "=")),
        )),

        // A let binding inside let_expression. Two body forms, chosen by the scanner
        // right after `=`:
        //   Indented:  let x =\n    body         (scanner emits _indent/_dedent)
        //   Inline:    let x = body              (scanner emits _inline_open/_close)
        // For the inline form, _inline_open records the body's start column and
        // _inline_close fires at the next line whose column is <= that — F#'s offside
        // rule for sibling lets and continuation expressions.
        let_decl_indented: ($) => seq(
            "let",
            optional("rec"),
            $._let_signature,
            "=",
            choice(
                seq($._indent, field('body', $._expression), $._dedent),
                seq($._inline_open, field('body', $._expression), $._inline_close),
            ),
        ),

        // Inner let used in expression positions. Two forms:
        //   Offside:      let x = body \n continuation     (via let_decl_indented)
        //   Explicit in:  let x = body in continuation
        // Branch B is required because Branch A alone can't express the single-line
        // form (no newline to delimit body and continuation).
        let_expression: ($) => prec.right(PREC.LET_EXPR,
            choice(
                seq(
                    field('binding', $.let_decl_indented),
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

        // `use r = resource` — auto-disposes r at end of enclosing scope.
        use_expression: $ => prec.right(PREC.LET_EXPR,
            seq("use", field('name', $.identifier), "=", $._expression),
        ),

        // `expr.Member` — member access when the LHS can't extend long_identifier.
        // For plain-identifier LHS, long_identifier (prec.right DOT) keeps `A.B.C`
        // as a single node; dot_expression only kicks in for compound LHS like
        // index_expression or application_expression.
        dot_expression: $ => prec(PREC.DOT, seq(
            field('object', $._expression),
            ".",
            field('member', $.identifier),
        )),

        // arr.[0]  arr.[1..2]  arr.[..2]  arr.[1..]  dict.["k"]  m.[0, 1]
        // `.[` is a single terminal so it never conflicts with the `.` in long_identifier.
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

        // `expr..`, `..expr`, or `expr..expr` inside index args. Preferred over
        // binary_expression's `..` alternative (which has no rhs before `]` or `,`).
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

        // new TypeName(args)  or  new TypeName<T>(args)
        // Type is restricted to long_identifier/generic_type so the `(` can't be
        // consumed as a parenthesized_type.
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
        // The leading `new` keyword disambiguates from record_expression.
        //
        // The `with`-body uses `_class_body_member` (the same rule that fills
        // `type_decl`/`type_extension` bodies). Object expressions don't allow
        // every class-body form (e.g. `val mutable`, `new(…)`, `let`, `do` are
        // all invalid here), but accepting them at parse time and letting the
        // F# compiler reject the invalid combinations is fine for a syntax
        // grammar — and keeping a single member-list rule avoids drift.
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
                repeat($._class_body_member),
            )),
            "}",
        ),

        // ── Exceptions ────────────────────────────────────────────────────────

        // exception MyErr  or  exception MyErr of string * int
        exception_decl: $ => prec.dynamic(1, seq(
            repeat(choice($.attribute, $.xml_doc_comment, $.block_doc_comment)),
            "exception",
            field('name', $.identifier),
            optional(seq("of", $.type_expression)),
        )),

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
        //
        // Body is `optional($._expression)` — NOT `_indented_or_inline_body` like
        // `if_expression`/`while_expression`/`lambda_expression`. Reason: a bare
        // `for x in xs do` can be followed by a query-CE custom operator
        // (`query { for x in xs do where … select … }`). When the body uses
        // `_indented_or_inline_body`, the scanner emits `_body_indent` for the
        // next line and the parser commits to a body parse — but the reserved
        // `query_ce` keyword set doesn't propagate through `_body_indent`'s
        // state boundary, so `where` becomes a plain identifier and gets eaten
        // as an application-expression body. Sticking with `$._expression`
        // keeps `where`/`select` reserved at the body slot, so the parser
        // correctly leaves the body empty and treats them as `query_operator`
        // siblings in the CE.
        //
        // Trade-off: a non-CE `for` with a multi-statement body parses as a
        // single chained application instead of `sequence_expression`. The
        // existing test corpus only exercises single-statement for-bodies and
        // this matches the pre-change behavior; revisit only if multi-statement
        // for-bodies become important.
        for_expression: $ => prec.right(PREC.IF_EXPR, seq(
            "for",
            choice(
                seq(
                    choice($.identifier, $.wildcard_pattern, $.tuple_pattern, $.record_pattern),
                    "in", $._expression, "do", optional(field('body', $._expression)),
                ),
                seq(
                    $.identifier,
                    "=", $._expression, choice("to", "downto"), $._expression, "do",
                    optional(field('body', $._expression)),
                ),
            ),
        )),

        // while cond do body   (imperative loop, returns unit)
        // Two-branch form (like `for_expression`); body-absent branch covers both
        // mid-edit (`while cond do`) and the parser-recovery case where the body
        // can't be the next token.
        while_expression: $ => prec.right(PREC.IF_EXPR, choice(
            prec(2, seq(
                "while", $._expression, "do",
                field('body', $._indented_or_inline_body),
            )),
            prec(1, seq("while", $._expression, "do")),
        )),

        // `builder { ... }` — async, task, seq, promise, query, or any custom CE.
        // CE_EXPR < APP_EXPR so `f { field = val }` is parsed as application with a
        // record argument when both forms are viable.
        // `reserved('query_ce', …)` activates the query-CE custom-operator names
        // (`select`, `where`, `join`, …) as their own tokens inside the body — they
        // remain plain identifiers in every other parse state.
        computation_expression: $ => prec(PREC.CE_EXPR,
            seq(
                field('builder', $.long_identifier),
                "{",
                repeat(reserved('query_ce', $._ce_statement)),
                "}",
            ),
        ),

        // Statements inside a CE body. `_expression` at the end covers
        // return/yield/return!/yield!/do!/for/while/if/match/etc.
        // The query_* alternatives only match in CE bodies because their leading
        // keywords are in the `query_ce` reserved set, which is activated by the
        // `reserved('query_ce', …)` wrap in `computation_expression`.
        _ce_statement: $ => choice(
            $.ce_let_bang_expr,
            $.use_binding,
            $.ce_use_bang_expr,
            $.ce_match_bang_expr,
            $.let_binding,
            $.do_stmt,
            $.query_operator,
            $.query_join_operator,
            $.query_group_by_operator,
            $.query_left_outer_join_operator,
            $._expression,
        ),

        // Query-CE custom operators that take a single expression argument:
        //   `select expr`  `where expr`  `sortBy keyExpr`  `take n`  …
        // These names are reserved only inside `computation_expression` (see the
        // `query_ce` reserved set + `reserved('query_ce', …)` wrap), so usages like
        // `List.where`, `let take n = …` outside any CE keep their identifier shape.
        query_operator: $ => prec.right(seq(
            field('op', choice(
                "select", "where", "sortBy", "sortByDescending",
                "thenBy", "thenByDescending", "take", "skip",
                "takeWhile", "skipWhile", "distinct", "count",
                "head", "last", "exactlyOne",
                "minBy", "maxBy", "sumBy", "averageBy",
                "find", "exists", "all", "contains", "nth",
                "headOrDefault", "lastOrDefault", "exactlyOneOrDefault",
            )),
            optional($._expression),
        )),

        // `join name in source on (key1 = key2)`
        query_join_operator: $ => seq(
            "join",
            field('name', $.identifier),
            "in",
            field('source', $._expression),
            "on",
            field('condition', $._expression),
        ),

        // `groupBy keyExpr into groupName`  (also `groupValBy v k into g`, `groupJoin …`)
        query_group_by_operator: $ => seq(
            choice("groupBy", "groupValBy", "groupJoin"),
            field('key', $._expression),
            "into",
            field('into', $.identifier),
        ),

        // `leftOuterJoin name in source on (key1 = key2) into groupName`
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

        // Bindable names for `let!` / `and!` — narrower than _let_name_pattern
        // (no operator names, active patterns, lists, or arrays).
        _ce_bang_name_pattern: $ => choice(
            $.identifier, $.typed_pattern, $.tuple_pattern, $.struct_tuple_pattern, $.unparenthesized_tuple_pattern, $.record_pattern, $.wildcard_pattern,
        ),

        // let! x = expr [and! y = expr ...]  — parallel applicative binding
        ce_let_bang_expr: $ => prec.right(PREC.LET_DECL,
            seq("let!", field('name', $._ce_bang_name_pattern), "=", $._expression,
            repeat($.ce_and_bang_expr),
            ),
        ),

        // and! y = expr  — continuation of a parallel let! group
        ce_and_bang_expr: $ => prec.right(PREC.LET_DECL,
            seq("and!", field('name', $._ce_bang_name_pattern), "=", $._expression),
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

        // return/yield/do! forms — in _expression so they're valid inside CE if/match branches.
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

        // Top-level patterns accept both `,` (tuple element) and `|` (or-alternative)
        // as separators: `| 1, 2 | 3, 4 -> "yes"` produces 4 sibling patterns rather
        // than nesting `2 | 3` inside or_pattern. prec(2) > or_pattern's prec(1) so
        // the repeat at `|` wins over reducing to or_pattern.
        //
        // The leading `|` is optional to support single-arm forms like
        // `try x with _ -> 0` and `match x with 0 -> "z" | _ -> ...`.
        match_arm: ($) => seq(
            optional("|"),
            $.pattern,
            repeat(prec(2, seq(choice(",", "|"), $.pattern))),
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

        // Constructor(field = pat; field2 = pat2)  — named DU field pattern.
        // prec.dynamic prefers starting a new field over extending the previous
        // pattern via identifier_pattern's constructor-application form.
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

        // pat1 | pat2  — alternative patterns. prec.left(1) binds tighter than `as`
        // (0) and looser than `::` (2).
        or_pattern: $ => prec.left(1, seq($.pattern, "|", $.pattern)),

        // (pat : type)  — type annotation on a pattern, always parenthesised.
        typed_pattern: $ => seq("(", $.pattern, ":", $.type_expression, ")"),

        // { Field = pat; Field2 = pat2 }  — destructure a record. prec.dynamic prefers
        // starting a new field over extending the previous value via identifier_pattern.
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
        // Restricted type (no function_type) so `->` stays available as the match-arm
        // separator. Function types in patterns need explicit parens: `:? (int -> string)`.
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

        // x :: rest  — right-assoc; prec 2 > or_pattern (1) > as_pattern (0).
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

        // Elements of unparenthesized_tuple_pattern. Excludes identifier_pattern's
        // constructor-application form which would otherwise consume `add a b` in
        // `let add a b = ...` before the parser sees there's no `,`.
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

        // `a, b` or `a, b, c` — bare tuple pattern without outer parens. Valid as the
        // bound name in let/let!/and!. Deliberately NOT included in $.pattern: match
        // arms handle commas via their own repeat.
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

        // Parameter forms for let/member bindings. The two `prec(20)` branches keep
        // single-identifier parenthesised forms flat (one parameter node containing
        // an identifier) rather than nested in tuple_params/tuple_pattern, matching
        // how curried functions and property accessors are usually written.
        parameter: $ => choice(
            $.identifier,
            $.unit,
            $.wildcard_pattern,
            // `([<Attr>] x: int)` / `([<Attr>] x)` — attributes on curried params
            // (used for ParamArray, optional/caller-info attributes outside tuple
            // form, etc.).
            prec(20, seq("(", repeat($.attribute), $.identifier, ":", $.type_expression, ")")),
            prec(20, seq("(", repeat($.attribute), $.identifier, ")")),
            $.tuple_params,                // (x: int, y: int) — OOP-style multi-param
            $.destructure_parameter,       // ((a,b): int*int)   ({X=x}: Point)
            $.tuple_pattern,               // (a, b)   (Some x)
            $.record_pattern,              // { X = x }
        ),

        // (pattern : type) where the inner pattern is a destructuring form. Bare
        // identifier form `(x: int)` is handled inline in `parameter` above.
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

        // Compound measure expressions: `m/s` `kg*m/s^2` `'u` `1`. Juxtaposition
        // (`kg m`) is unsupported — write `kg*m` instead.
        measure_expression: $ => choice(
            prec.left(1, seq($.measure_expression, "/", $.measure_expression)),
            prec.left(2, seq($.measure_expression, "*", $.measure_expression)),
            $.measure_power_type,
            $.int_literal,
            $.type_parameter,
            $.long_identifier,
        ),

        // `3.0<cm>` `55.0<miles/hour>` `3u<days>`. token.immediate(`<`) ensures no
        // whitespace before `<`, distinguishing this from comparison expressions.
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

        // `(|Even|Odd|)` `(|Integer|_|)` `(|Single|)` — single terminal so the lexer
        // never splits `(|` as `(` + `|`, which would break `let (|>) a b = …`.
        active_pattern_name: _ => token(seq(
            "(|",
            /[a-zA-Z_][a-zA-Z0-9_']*/,
            repeat(seq("|", /[a-zA-Z_][a-zA-Z0-9_']*/)),
            optional(seq("|", "_")),
            "|)",
        )),

        // Top-level token: anything that can sit at module / source-file scope
        // or inside a `module Foo =` body (which uses `repeat($._token)`).
        //
        // Class-body-only declarations (member_defn, secondary_constructor,
        // abstract_member_defn, interface_impl, inherit_decl, val_field) are
        // NOT listed here — they parse exclusively as `_class_body_member`
        // children of `type_decl`/`type_extension`. That nesting is what makes
        // expand-selection (member → type → file) work and lets indent rules
        // distinguish "inside a member's body" from "between members".
        _token: $ => choice(
            // Script-only header
            $.shebang,

            // Preprocessor directives (conditional + non-structural)
            $.preproc_if,
            $.preproc_directive,

            // Attribute attached to the following declaration
            $.attribute,

            // Scope declarations
            $.namespace_decl,
            $.module_decl,
            $.import_decl,         // `open Foo`

            // Type-level declarations
            $.type_decl,
            $.type_extension,
            $.exception_decl,

            // Value-level declarations
            $.let_binding,
            $.use_binding,
            $.do_stmt,

            // Comments (also extras, but listed so they can stand as a child
            // of source_file / module body when surrounded by other tokens)
            $.xml_doc_comment,
            $.line_comment,
            $.block_comment,
            $.block_doc_comment,

            // Bare expression statements (last so all the above forms win
            // when their leading keyword/punctuation is unambiguous)
            $._expression,
        ),

        // Plain identifiers and `` `any text` ``-quoted form, unified in one terminal.
        // `word: $.identifier` still drives keyword detection; backtick forms never
        // match keywords because the regex requires the backticks.
        identifier: _ => token(choice(
            /[a-zA-Z_][a-zA-Z0-9_']*/,
            /``[^`\n\r\t]+``/,
        )),

        // prec.right(DOT) beats the REDUCE of _expression so chains like `A.B.C` stay
        // as a single long_identifier rather than being split by dot_expression.
        long_identifier: $ =>
            prec.right(PREC.DOT,
                seq(
                    $.identifier,
                    repeat(seq(".", $.identifier)),
                ),
            ),

        // Number bases.
        _int: _ => token(/[0-9][0-9_]*/),
        _hex: _ => token(seq(choice("0x", "0X"), /[0-9a-fA-F_]+/)),
        _oct: _ => token(seq(choice("0o", "0O"), /[0-7_]+/)),
        _bin: _ => token(seq(choice("0b", "0B"), /[01_]+/)),

        // Number suffixes (immediate = no whitespace before suffix). `m`/`M`/`f`/`F`
        // are accepted as integer suffixes too so that `3f` and `42m` work as
        // dot-less float / decimal literals; themes rarely distinguish them anyway.
        _int_suffix: _ => token.immediate(choice("uy", "us", "uL", "UL", "Ul", "ul", "un", "u", "y", "s", "l", "L", "n", "I", "m", "M", "f", "F")),
        _float_suffix: _ => token.immediate(choice("f", "F", "m", "M")),

        int_literal: $ => seq(
            choice($._hex, $._oct, $._bin, $._int),
            optional($._int_suffix),
        ),

        // The decimal-point form requires digits on both sides so that `1..10` lexes
        // as `int + .. + int` rather than `float(1.) + . + int(10)`.
        float_literal: $ => seq(
            choice(
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

        // Body chunks of interpolated strings. `%` is excluded so printf-style
        // `%fmt{` specifiers tokenise separately.
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

        // Triple-quoted interpolated: the `"[^…]` alternatives prevent greedy match
        // from eating the closing `"""`.
        _interp_triple_text: _ => token.immediate(repeat1(choice(
            /[^"{}%]+/,
            /""[^"{}%]/,
            /"[^"{}%]/,
            '{{',
            '}}',
        ))),

        // Body of `:fmt` inside `{expr:fmt}`, up to the closing `}`.
        _interp_format_spec: _ => token.immediate(/[^}]+/),

        // Literal `%` that isn't the start of a valid printf format spec.
        _interp_percent: _ => token.immediate('%'),

        // Printf-style format prefix `%[flags][width][.precision]conv{`. The trailing
        // `{` is required so `100% done` (no following `{`) doesn't match.
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

        // Non-nesting block comment. Nested comments `(* (* … *) *)` aren't supported —
        // the outer closes at the first `*)`. Keeping this as a single token() avoids a
        // recursive extras rule, which would inflate every parser state's item set.
        block_comment: _ => token(seq("(*", /([^*]|\*+[^)*])*\*+/, ")")),

        // Starts with `(**`. prec(1) wins over block_comment when both match the same
        // length (e.g. `(** doc *)` matches both).
        block_doc_comment: _ => token(prec(1, seq("(**", /([^*]|\*+[^)*])*\*+/, ")"))),


        // Non-structural directives: `#nowarn`, `#r`, `#load`, `#line`, … Structural
        // directives `#if/#elif/#else/#endif` use dedicated higher-priority tokens.
        preproc_keyword: _ => token(seq("#", /[a-zA-Z_][a-zA-Z0-9_]*/, /[ \t]*/)),

        preproc_directive: $ => prec.right(seq(
            field('name', $.preproc_keyword),
            optional(field('argument', choice($.string_literal, $.int_literal, $.long_identifier))),
        )),

        // Unix-style shebang at the top of an `.fsx` script — `#!/usr/bin/env -S dotnet fsi`.
        // Matches `#!` followed by anything up to (but not including) the newline.
        // `#!` doesn't conflict with `preproc_keyword` (which requires an identifier
        // after `#`) or with `#if`/`#elif`/etc.
        shebang: _ => token(seq("#!", /[^\n\r]*/)),

        // Structural directives — prec(1) > preproc_keyword's prec 0 when both match
        // the same string. Longer matches still win, so `#ifdef` falls to preproc_keyword.
        preproc_if_kw: _ => token(prec(1, seq("#if", /[ \t]*/))),
        preproc_elif_kw: _ => token(prec(1, seq("#elif", /[ \t]*/))),
        preproc_else_kw: _ => token(prec(1, seq("#else", /[ \t]*/))),
        preproc_endif_kw: _ => token(prec(1, seq("#endif", /[ \t]*/))),

        // Boolean condition for `#if`/`#elif`. `&&` binds tighter than `||`.
        preproc_expression: $ => choice(
            $.identifier,
            "true",
            "false",
            seq("!", $.preproc_expression),
            seq("(", $.preproc_expression, ")"),
            prec.left(2, seq($.preproc_expression, "&&", $.preproc_expression)),
            prec.left(1, seq($.preproc_expression, "||", $.preproc_expression)),
        ),

        // #if COND  body  [#elif COND  body]  [#else  body]  #endif
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
