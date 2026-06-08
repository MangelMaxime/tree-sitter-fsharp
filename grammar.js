/**
 * @file F# tree sitter definition focused on Helix
 * @author Mangel Maxime
 * @license Apache 2.0
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

// Optional prefix on every decoratable declaration — zero or more attributes
// (`[<…>]`). Used at the top of `let_binding`, `module_decl`, `member_defn`,
// `abstract_member_defn`, `exception_decl`, `secondary_constructor`.
//
// Doc comments (`///` / `(** *)`) are deliberately NOT here: as a regular token
// a doc was grabbed as a sibling decl and ended the enclosing rule early (e.g.
// detaching a documented type's `and` clause). They stay EXTRAS-ONLY — still in
// the tree, but never terminating a rule. Cost: a leading `///` doesn't nest in
// its decl, so `maf`/`mat` won't select it (attributes still nest).
function decoration($) {
    return repeat($.attribute);
}

// "Indented or inline" field list — a record-like body that's either:
//   - multi-line: `_body_indent` (scanner pushes the field column) + fields
//     separated by `_virtual_semi` (or explicit `;`) + `_body_dedent`;
//   - single-line: explicit `;` only.
// Used by both `_record_fields` (record / anonymous-record EXPRESSIONS,
// containing `record_field`s) and `record_type_defn`'s body (record TYPE
// declarations, containing `record_type_field`s). The shared structure
// prevents drift; the two callers pass their own field rule and the
// dynamic-prec used to break the field-vs-postfix-application conflict.
function indentedOrInlineFieldList($, field, sepPrec, opts) {
    return choice(
        // Block / newline-aligned form, covering both `{ F1\n F2 }` (`{` on the
        // first field's line) and `{\n F1\n F2\n}`. `_record_open` peeks for a
        // field (`ident =`/`ident :`), captures its column, and is SUPPRESSED for
        // `{ new …}` (object expr) and `{ base with …}` (copy-update) — so those
        // grammar branches match instead. Fields separate by `_bracket_semi` (a
        // dedicated token a nested sequence can't steal) or explicit `;`; the
        // body pops on `_bracket_close` at `}`.
        seq(
            $._record_open,
            field,
            // No prec.dynamic on the separator: `_bracket_semi` is a dedicated
            // token an application value can't absorb, so it already stops a field
            // value from swallowing the next field. A prec above the field's
            // application would WRONGLY end the value at its head (`X = abs 3` →
            // `X = abs`, dropping the arg).
            repeat(seq(choice(";", $._bracket_semi), field)),
            optional(choice(";", $._bracket_semi)),
            $._bracket_close,
        ),
        // `{ F1; F2; F3 }` — single line, explicit `;` separators only (used when
        // `_record_open` doesn't fire, e.g. inside a copy-update's field list).
        seq(
            field,
            repeat(prec.dynamic(sepPrec, seq(";", field))),
            optional(";"),
        ),
    );
}

export default grammar({
    name: "fsharp",

    word: $ => $.identifier,

    // Supertypes — hidden choice rules promoted to queryable categories in
    // node-types.json. Purely additive: the tree shape is unchanged (the
    // concrete subtype still appears; the supertype stays hidden), but queries
    // can now match `(_expression)` / `(_simple_expression)` / `(_literal)`
    // instead of enumerating every alternative. `_expression` nests `_literal`
    // (a supertype may contain another supertype).
    supertypes: $ => [
        $._expression,
        $._simple_expression,
        $._literal,
    ],

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
        $._error_sentinel,    // unused in rules; valid only on all-symbols-valid (recovery)
        $._layout_open,       // generic body open (Decl/Then/Do/Let) after =/then/else/->/do
        $._layout_semi,       // generic separator (next line == body col)
        $._layout_end,        // generic close (next line < body col)
        $._match_open,        // arm-list open after with/function/(lambda)->
        $._match_end,         // arm-list close (dedent below arm col, or == col & not `|`)
        $._bracket_open,      // [ / [| / { block body on its own line(s)
        $._bracket_semi,      // newline-aligned element/field separator
        $._bracket_close,     // ] / |] / } closing a block bracket
        $._record_open,       // `{` record body — peeks `ident =`/`ident :`; not new/copy-update
        $._block_open,        // newline-gated layout open for MODULE bodies (closes via _layout_end)
        $._type_open,         // newline-gated layout open for TYPE bodies (also closes before `with`)
        $._expr_open,         // expression body (then/elif body, lambda, let-in value); closes before else/elif/in
        $._else_open,         // final-else body; suppressed when next token is `if` (→ flat else-if)
        $._float_trailing_dot,
        // Interpolated-string TEXT chunks. External (not token.immediate) so the
        // scanner consumes them BEFORE tree-sitter's extra-skipping — otherwise a
        // leading `//` (e.g. `$"//# sourceMappingURL={…}"`) is lexed as a
        // `line_comment` extra and corrupts the string. See scan_interp_text().
        $._interp_string_text,
        $._interp_verbatim_text,
        $._interp_triple_text,
        $._for_open,          // `for … do` body open (suppressed before query-CE operators)
        $._ctor_attr,         // zero-width gate: an attribute on a primary ctor (`type T [<ParamObject>] (…)`) — only when `[<…>]+ (` follows
        $._try_open,          // try/finally body open (S_TRY) — closes before `with`/`finally`
        $._label_attr,        // zero-width gate: attribute on a labelled param (`[<ParamArray>] xs: obj[]`) — only when `[<…>]+ ident:` follows
    ],

    extras: $ => [/\s+/, $.xml_doc_comment, $.line_comment, $.block_comment, $.block_doc_comment,
        // Conditional-compilation directives are skippable anywhere (see preproc_if).
        $.preproc_if, $.preproc_elif, $.preproc_else_kw, $.preproc_endif_kw],

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
        // token could be a standalone `_decl_or_comment` child OR the start of
        // a decl's decoration prefix. GLR explores both; we bias toward
        // attachment via `prec.dynamic` on the decl branch.
        [$._decl_or_comment, $.let_binding, $.module_decl, $.exception_decl],
        // Same situation inside a class/type body — `[<…>]` or `///` could be
        // a standalone `_class_body_member` (via `_decl_or_comment`) or the
        // start of any decoratable member's prefix.
        [$._decl_or_comment, $.let_binding, $.member_defn, $.abstract_member_defn, $.secondary_constructor, $.val_field],
        // `expr <` may begin a `type_application_expression`
        // (`Map.empty<string, int>`) or a `<` comparison in
        // `binary_expression`. GLR explores both; type_application only
        // succeeds when `<…>` contains `type_expression , type_expression
        // … >`, otherwise binary wins.
        [$.type_application_expression, $._expression],
        [$.type_application_expression, $._simple_expression],
        // `( x` opening a pattern could be a plain tuple_pattern (first element a
        // pattern) or a tuple_typed_first_pattern (first element `x: type`). GLR
        // explores both; the `:` after the first element decides.
        [$.pattern, $.tuple_typed_pattern],
        [$.identifier_pattern, $.tuple_typed_pattern],
        // `name: T` is ambiguous: a member-signature `labelled_type`, a plain
        // type in another context (`sizeof<…>`), or a union field (`of name: T`).
        // GLR explores them; the enclosing construct selects the right one. Two
        // declarations cover the 2-way (type contexts) and 3-way (union) states.
        [$.labelled_type, $.type_expression],
        [$.labelled_type, $._union_field_type],
        [$.labelled_type, $._union_field_type, $.type_expression],
        // `#Foo<int>` — the `<` could extend `Foo` into a `generic_type` inside the
        // flexible type, or (after `#Foo`) start a comparison. GLR explores both;
        // in a type position the generic form wins.
        [$.flexible_type, $.generic_type],
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
        module_decl: $ => seq(
            decoration($),
            "module",
            optional($.access_modifier),
            optional("rec"),
            field('name', $.long_identifier),
            optional(seq("=", optional(choice(
                field('abbrev', $.long_identifier),
                seq(
                    $._block_open,
                    repeat($._token),
                    $._layout_end,
                ),
            )))),
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
            // Optional target specifier: `[<return: Struct>]`, `[<assembly:
            // AssemblyVersion(…)>]`, `[<param: …>]`. `return`/`module`/`type` are
            // keyword tokens; the rest lex as plain identifiers.
            optional(seq(
                field('target', choice($.identifier, "return", "module", "type")),
                ":",
            )),
            field('name', $.long_identifier),
            optional(choice(
                // `[<Foo(args)>]` — parenthesised constructor arguments. The `( )`
                // boundary lets these be ARBITRARY expressions (named args `X=y`,
                // arithmetic, arrays, `typeof<…>`, several comma-separated args).
                seq(
                    "(",
                    optional(seq($._expression, repeat(seq(",", $._expression)))),
                    ")",
                ),
                // `[<Direct @"…">]` / `[<Foo "x">]` — bare single-argument form
                field('argument', $._attribute_arg),
            )),
        ),

        // Atomic argument for the bare (no-parens) attribute form. Deliberately
        // narrower than `_expression`: only forms that neither start with `(`
        // (would clash with the parenthesised attribute form) nor contain `>`
        // (would clash with the `>]` that closes the attribute). `unit`, `measure`,
        // and the bracket/paren/type-application expressions are excluded for that
        // reason. See `attribute_target` for why this can't be unified with the
        // parenthesised branch.
        _attribute_arg: $ => choice(
            $.int_literal, $.float_literal, $.char_literal,
            $.string_literal, $.verbatim_string, $.triple_quoted_string,
            $.interpolated_string, $.interpolated_verbatim_string,
            $.interpolated_triple_string, $.bool_literal, $.long_identifier,
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
            // `type private Foo = …` — visibility of the TYPE itself, before
            // the name. Distinct from the slot below (which controls
            // visibility of the primary CONSTRUCTOR).
            optional($.access_modifier),
            // ML-style prefix type parameters: `type 'T set = …`,
            // `type ('a, 'b) pair = …`. Alternative to the postfix `<…>` list.
            optional($.prefix_type_parameters),
            field('name', $.identifier),
            optional($.type_parameter_list),
            // `type Foo<'T> when 'T: comparison = …` — constraints may also sit
            // OUTSIDE the `<…>` list, between it and `=`.
            optional($._when_constraints),
            // `type Foo private (...)` — F# allows an access modifier between
            // the type-parameter list and the primary constructor (controls
            // who can call the constructor, not visibility of the type).
            // Group with primary_constructor so the `(` lookahead sees a
            // single optional alternative (high prec) rather than two
            // independent optionals — helps when extras (comments) sit
            // between the name and the constructor.
            optional(prec(20, seq(
                optional($.access_modifier),
                $.primary_constructor,
            ))),
            // `as this` — names the constructed instance so the body can refer
            // back to it. Identifier is conventionally `this` but any name is
            // legal (`as self`, etc.).
            optional(seq("as", field('self', $.identifier))),
            // Augmentation `with member …` can ONLY follow when `=` is present.
            // Without `=`, the `with` belongs to `type_extension` instead
            // (`type Foo with …` — extending an already-declared type).
            optional(seq(
                "=",
                optional($._type_decl_body_or_class),
                optional($._type_augmentation),
            )),
            repeat($.type_and_decl),
        )),

        // and Even = ...  (mutual type recursion continuation)
        type_and_decl: $ => prec.right(seq(
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
            optional(seq(
                "=",
                optional($._type_decl_body_or_class),
                optional($._type_augmentation),
            )),
        )),

        // Trailing `with member …` after a type definition body — F#'s
        // "type augmentation" form, adding members at the point of declaration:
        //   type Point = { X: int; Y: int } with
        //       member this.Magnitude = …
        // The members become children of `type_decl` (same as the regular class
        // body). Two body shapes: INDENTED on the next line(s) (`_body_indent`),
        // or INLINE on the same line as `with` (`type Index = Index of string with
        // interface IIndex with member …`). Distinct from `type_extension`
        // (`type Foo with …` with no `=`), which augments from outside.
        _type_augmentation: $ => prec.right(seq(
            "with",
            optional(choice(
                seq($._layout_open, repeat($._class_body_member), $._layout_end),
                $._class_body_member,
            )),
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
                $._layout_open,
                repeat($._class_body_member),
                $._layout_end,
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
                $._type_open,
                choice(
                    // Record / union / enum / etc. body, OPTIONALLY followed
                    // by augmentation members in the same indented block
                    // (no `with` keyword). Valid F#:
                    //   type Project =
                    //       | A
                    //       | B
                    //       static member ofString s = …
                    seq($._type_decl_body, repeat($._class_body_member)),
                    repeat1($._class_body_member),
                ),
                $._layout_end,
            ),
        ),

        // Choice alternatives shared by `_token` (source-level / module body)
        // and `_class_body_member` (type body). Both contexts allow attributes
        // and top-level value declarations (let/do). Tree-sitter inlines
        // hidden rules in choice positions, so the parent's children still
        // appear directly as `attribute`, `let_binding`, etc. — no extra
        // wrapping node.
        //
        // No comment forms here, INCLUDING doc comments — see `decoration()`:
        // a comment as a regular token terminates the enclosing rule early
        // (detaches `type … and …`, breaks primary_constructor). Doc comments
        // stay extras-only.
        _decl_or_comment: $ => choice(
            $.attribute,
            $.let_binding,
            $.do_stmt,
        ),

        // Everything legal inside a class or type-extension body.
        _class_body_member: $ => choice(
            $.inherit_decl,
            $.member_defn,
            $.abstract_member_defn,
            $.interface_impl,
            $.secondary_constructor,
            $.val_field,
            $._decl_or_comment,
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
        // Primary constructor for class types: `type T()` / `type Dog(name, …)`,
        // and `type T [<ParamObject; Emit>] (…)` (Fable interop — attributes on the
        // ctor). The attribute form is gated by the scanner token `_ctor_attr`,
        // emitted ONLY when `[<…>]+` is immediately followed by `(`. That avoids
        // mis-grabbing a standalone attribute on the NEXT declaration
        // (`[<Measure>] type cm`⏎`[<Measure>] type kg`, where `[<Measure>]` is
        // followed by `type`, not `(`).
        primary_constructor: $ => prec(20, choice(
            $.unit,
            seq(
                optional(seq($._ctor_attr, repeat1($.attribute))),
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
            decoration($),
            optional($.access_modifier),
            "new",
            field('parameters', $.tuple_params),
            "=",
            // Layout body so it closes at the next ctor/member instead of
            // absorbing it (two `new …` in a row).
            seq($._layout_open, field('body', $._expression), $._layout_end),
            optional(seq("then", seq($._layout_open, $._expression, $._layout_end))),
        ))),

        // `: TypeExpr` return-type annotation. Shared by let_binding, let_and_binding,
        // let_decl_indented, let_expression Branch B, _method_body, and auto-properties.
        //
        // F# allows a trailing `when …` constraints clause after the return
        // type — used in inline SRTP functions to attach member constraints
        // outside an explicit `<…>` type-parameter list, e.g.
        //   let inline replace (a: ^a) : ^b
        //       when (CFunctor or ^b) : (static member replace: ^a * ^b -> ^b)
        //       = …
        _return_type_annot: $ => seq(
            ":",
            field('return_type', choice($.type_expression, $.nullable_type)),
            optional(seq(
                "when",
                $.type_constraint,
                repeat(seq("and", $.type_constraint)),
            )),
        ),

        // `member/override/default [inline] [access] self.Name[<'T,…>]` —
        // shared by method and property forms. Optional `type_parameter_list`
        // lets generic methods like `member this.Map<'T>(x: 'T) = x` parse.
        // `access_modifier` (`member inline internal _.P () = …`) controls
        // the member's visibility independently of the type's.
        _instance_member_prefix: $ => seq(
            choice("member", "override", "default"),
            optional("inline"),
            optional($.access_modifier),
            field('self', $.member_self_ident),
            ".",
            field('name', choice($.identifier, $.operator_name)),
            optional($.type_parameter_list),
        ),

        // `static member [inline] Name[<'T,…>]` — shared by method and property
        // forms. F# accepts `inline` between `member` and the name (the typical
        // placement, e.g. `static member inline Add x y = x + y`).
        //
        // `operator_name` is accepted as the name so operator overloads like
        // `static member (>) (a, b) = …` parse as members instead of
        // generating cascading errors that break downstream highlighting.
        _static_member_prefix: $ => seq(
            "static",
            "member",
            optional("inline"),
            optional($.access_modifier),
            field('name', choice($.identifier, $.operator_name)),
            optional($.type_parameter_list),
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
            // Uniform layout body (closes at the next member). `optional` keeps
            // `member X =` (mid-edit, no body yet) parseable.
            optional(seq($._layout_open, field('body', $._expression), $._layout_end)),
        )),

        // `with get/set accessor [and get/set accessor]` — shared by property forms.
        _accessor_body: $ => prec.right(seq(
            "with",
            $.property_accessor,
            optional(seq("and", $.property_accessor)),
        )),

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
                decoration($),
                $._instance_member_prefix, $._method_body,
            )),
            prec.dynamic(1, seq(
                decoration($),
                $._static_member_prefix, $._method_body,
            )),
            prec.dynamic(1, seq(
                decoration($),
                $._instance_member_prefix, $._accessor_body,
            )),
            prec.dynamic(1, seq(
                decoration($),
                $._static_member_prefix, $._accessor_body,
            )),
            // Auto-property — instance/static differ only by the `static` prefix.
            prec.dynamic(1, seq(
                decoration($),
                optional("static"),
                "member",
                "val",
                field('name', $.identifier),
                optional($._return_type_annot),
                "=",
                // Layout-bounded init expression. Without a body context the
                // init greedily sequences into the next member (`member val A =
                // x⏎ member val B = y` → `A = (x; …)`). Reuse the S_TRY opener
                // (`_try_open`): like a generic layout body it dedent-closes at
                // the next member, but it ALSO closes before an inline `with`, so
                // the `with get, set` accessor form still attaches.
                seq($._try_open, field('body', $._expression), $._layout_end),
                optional($.auto_property_accessors),
            )),
        ),

        // get() = expr  or  set(v) = expr  (inside a property definition).
        // `inline` may precede the accessor keyword
        // (`with inline get () = …` / `and inline set v = …`).
        // A return-type annotation is allowed after the parameters
        // (`with get (count : int) : string = …`).
        property_accessor: $ => seq(
            optional("inline"),
            choice("get", "set"),
            field('parameters', repeat($.parameter)),
            optional($._return_type_annot),
            "=",
            // Layout body (like every other `=` body) so it closes at the next
            // member/decl instead of absorbing it.
            seq($._layout_open, field('body', $._expression), $._layout_end),
        ),

        // with get [, set]  (auto-property accessor list)
        auto_property_accessors: _ => seq(
            "with",
            choice("get", "set"),
            optional(seq(",", choice("get", "set"))),
        ),

        member_self_ident: $ => $.identifier,

        // abstract member Name: TypeExpr                     — method or read-only property
        // abstract member Prop: int with get, set             — read-write property
        // abstract member F<'T>: 'T -> 'T                     — generic method
        // Reuses `auto_property_accessors` for the `with get [, set]` clause —
        // the syntax is identical to the one on member-val auto-properties.
        abstract_member_defn: $ => prec.dynamic(1, seq(
            decoration($),
            optional("static"),
            "abstract",
            optional("member"),
            field('name', $.identifier),
            optional($.type_parameter_list),
            ":",
            $.type_expression,
            optional($.auto_property_accessors),
        )),

        // inherit BaseClass(arg1, arg2) [as super]
        // Optional `as super` names the base instance — `super` is the
        // conventional identifier, but any name is legal. The bound name
        // shadows the built-in `base` keyword inside overrides:
        //   inherit Dog(name) as super
        //   override this.ToString() = super.ToString() + " - cat"
        inherit_decl: $ => prec.right(seq(
            "inherit",
            field('base', $.type_expression),
            optional(seq(
                "(",
                optional(seq($._expression, repeat(seq(",", $._expression)))),
                ")",
            )),
            optional(seq("as", field('alias', $.identifier))),
        )),

        // interface IFoo with                interface IBar with
        //     member this.A = …                  member _.B = …
        //
        // Same `_body_indent`/`_body_dedent` pattern as `type_extension`: the
        // member impls following `with` become children of the `interface_impl`
        // node, so expand-selection walks identifier → member_defn →
        // interface_impl → enclosing class/object_expression.
        interface_impl: $ => prec.right(seq(
            "interface",
            field('type', $.type_expression),
            // `with` members: INDENTED on the next line(s), or INLINE on the same
            // line (`interface IIndex with member this.X = …`).
            optional(seq(
                "with",
                optional(choice(
                    seq($._layout_open, repeat($._class_body_member), $._layout_end),
                    $._class_body_member,
                )),
            )),
        )),

        // do expr  (class initializer or module-level side effect)
        // static do runs once at type initialization time
        // Layout body so `[static] do expr` closes at the next member/statement
        // instead of absorbing it (e.g. `static do printfn …` before members).
        do_stmt: $ => seq(optional("static"), "do", $._expr_open, $._expression, $._layout_end),

        // Explicit field in a class:
        //   val mutable field: int
        //   [<DefaultValue>] val mutable field : int
        //   [<DefaultValue>] static val mutable private field : int
        val_field: $ => seq(
            decoration($),
            optional("static"),
            "val",
            optional("mutable"),
            optional($.access_modifier),
            field('name', $.identifier),
            ":",
            choice($.type_expression, $.nullable_type),
        ),

        // `optional(access_modifier)`: `type X = private | A | B` — a private (or
        // internal) union representation, the F# smart-constructor pattern.
        // F# allows the FIRST case to omit its `|`: `type X = A | B | C`. That bare
        // first case (no `|`, no attributes — `union_case_bare`, aliased to
        // `union_case` so queries see one node type) is followed by the usual
        // `|`-prefixed cases. The `| A | B` and multi-line `| A`⏎`| B` forms go
        // straight through `repeat1($.union_case)`. A bare first case is kept
        // attribute-LESS so it can't be confused with an attributed type member.
        union_type_defn: $ => seq(
            optional($.access_modifier),
            choice(
                // `| A | B` and multi-line `| A`⏎`| B` (leading-pipe form).
                repeat1($.union_case),
                // Bare FIELD-LESS first case: `A | B`. A field-less bare name
                // alone (`type X = A`) is a type abbreviation, so this form
                // REQUIRES at least one following `|`-case (the `repeat1`); a
                // lone bare name falls through to the alias branch.
                seq(
                    alias($.union_case_bare, $.union_case),
                    repeat1($.union_case),
                ),
                // Bare first case WITH fields: `A of B` — a valid SINGLE-case
                // union (no following `|` needed). `prec.dynamic` makes it win
                // over the alias (`type_expression`), which would otherwise also
                // match `A of B`.
                seq(
                    alias($.union_case_bare_fields, $.union_case),
                    repeat($.union_case),
                ),
            ),
        ),

        union_case_bare: $ => prec.right(seq(
            field('name', $.identifier),
            repeat($.line_comment),
        )),

        union_case_bare_fields: $ => prec.dynamic(2, prec.right(seq(
            field('name', $.identifier),
            "of", choice(
                $.union_case_named_fields,
                field('fields', $.type_expression),
            ),
            repeat($.line_comment),
        ))),

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
        // F# allows freely MIXING named and anonymous fields in any order, as
        // long as at least one is named (all-anonymous goes through the
        // `type_expression` tuple branch in `union_case` instead):
        //   | StepThumb of int * inProgress: bool * outcome: Outcome
        //                  ^anon   ^named            ^named
        // The leading `repeat` is the optional ANONYMOUS prefix before the first
        // named field; the required `union_case_field` anchors this rule to "has
        // a named field" (so it doesn't overlap the all-anonymous branch); the
        // trailing `repeat` is the mixed remainder. `_union_field_type` excludes
        // tuple_type, so each `*` is a field separator, never a tuple inside a
        // field.
        union_case_named_fields: $ => seq(
            repeat(seq(field('fields', $._union_field_type), "*")),
            $.union_case_field,
            repeat(seq("*", choice(
                $.union_case_field,
                field('fields', $._union_field_type),
            ))),
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

        // `name: T` / `?name: T` — a labelled element in a member/abstract
        // signature tuple, e.g. `abstract M: Context * selector: string *
        // ?noMangle: bool -> Ret`. It's an alternative of `type_expression` so the
        // surrounding `*`/`->` structure (and type highlighting) is reused; the
        // element type itself excludes `function_type`/`tuple_type` so the
        // signature's `->` and `*` aren't absorbed into the label.
        labelled_type: $ => seq(
            // `[<ParamArray>] xs: obj[]` — attribute on a labelled (member-sig)
            // parameter. Gated by `_label_attr` (emitted by the scanner only when
            // `[<…>]+ ident:` follows) so it stays a distinct token from a
            // member-decoration `[<…>]` and creates no type-body conflict.
            optional(seq($._label_attr, repeat1($.attribute))),
            optional("?"),
            field('name', $.identifier),
            ":",
            field('type', choice(
                $.postfix_type,
                $.generic_type,
                $.array_type,
                $.parenthesized_type,
                $.anonymous_record_type,
                $.type_parameter,
                $.long_identifier,
                $.flexible_type,        // `source: #TypedArray`
                // (nullable `value: string | null` not added here: labelled_type
                // is shared with union NAMED fields, where `|` is the case
                // separator — use `value: (string | null)` in member sigs.)
            )),
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

        // Body of `type Foo = { … }`. Two forms (indented or inline) via the
        // `indentedOrInlineFieldList` helper — the indented form prevents a
        // field's type from greedily absorbing the next field's name across
        // a newline (e.g. `unit -> unit` followed by `A : 'A` was parsed as
        // `unit -> (unit A)` via postfix_type, erroring on the trailing `:`).
        record_type_defn: $ => seq(
            // `type X = private { … }` — private record representation.
            optional($.access_modifier),
            "{",
            indentedOrInlineFieldList($, $.record_type_field, TYPE_PREC.POSTFIX + 1, { sameLineBraceForm: true }),
            "}",
        ),

        // prec(POSTFIX) on the field ties its REDUCE precedence with postfix_type's SHIFT
        // precedence, letting prec.dynamic in record_type_defn resolve the conflict.
        record_type_field: $ => prec(TYPE_PREC.POSTFIX, seq(
            optional("mutable"),
            field('name', $.identifier),
            ":",
            field('type', choice($.type_expression, $.nullable_type)),
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

        _expression: $ => choice(
            $.parenthesized_expression,
            $.typed_expression,
            $.application_expression,
            $.binary_expression,
            // `Map.empty<string, _>` — explicit type-argument application
            // on a value/function. Same lexical shape as binary `<` `>`
            // comparisons, so static prec(PAREN_EXPR) biases this form
            // when the `<…>` contains type-expressions with a comma.
            $.type_application_expression,
            $.unary_expression,
            // `(|Foo|)` / `Module.(|Foo|)` — active pattern as a value (`snd >> M.(|Foo|) >> g`).
            $.active_pattern_expression,
            // `(+) 1 2`, `(=) x y` — operator name applied to arguments.
            $.operator_application,
            // `(+)`, `(>>)` — a bare operator as a first-class value (`let add =
            // (+)`, `(+) >> id`). The applied form above wins when args follow.
            $._operator_value,
            // `not` as a first-class function value (`not >> g`, `not |> f`).
            $.not_function,
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
            // `use_expression` is the expression-position form of `use x = e`; it's
            // structurally identical to `use_binding` (the declaration form) but kept
            // a separate rule because the two live in different parse contexts
            // (expression vs `_token`/`_ce_statement`) with different precedence —
            // merging the rules creates an `_expression` vs `_token` conflict. Alias
            // its OUTPUT to `use_binding` so the tree (and queries) see one node type.
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
            // CE result forms — also valid in if/match branches inside CEs
            $.ce_result_expr,
            $.struct_tuple_expression,
            $.typed_quotation,
            $.untyped_quotation,
            $.optional_named_arg,
            $.address_of_expression,
            $.type_keyword_expression,
            $.sequence_expression,
        ),

        // F#'s implicit sequence — multiple expressions at the same indent inside
        // a body (function, if/then/else, for, while, lambda, …). The scanner
        // emits a zero-width `_virtual_semi` between expressions when a newline
        // crosses an offside-rule boundary; GLR exploration sorts out sequences
        // vs continuations.
        // Statement sequencing: newline-aligned (`_virtual_semi`, the scanner's
        // implicit sequence operator) OR an explicit `;` (`expr1; expr2`, e.g.
        // `visitor.Touch p; p`). `_virtual_semi` is never emitted inside brackets
        // (the scanner uses `_bracket_sep` there), so it can't clash with
        // list/array separators — but the literal `;` can, hence the higher
        // static precedence on the list/array `;` separators (see below) so
        // `[ a; b ]` stays two ELEMENTS, not one `(a; b)` sequence element.
        sequence_expression: $ => prec.left(PREC.SEQ_EXPR, seq(
            $._expression,
            repeat1(seq(choice($._layout_semi, ";"), $._expression)),
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

        // sizeof<'T>  typeof<'T>  typedefof<'T> — type-level intrinsics.
        // The keyword is fused with its REQUIRED adjacent `<` into a single
        // token so the bare words stay usable as plain identifiers everywhere
        // else (`let typeof = …`, `f (a, typeof, …)` — common in Fable). With
        // them as standalone string keywords the lexer (keyword-extraction on)
        // emits the keyword in any expression slot and dead-ends when no `<`
        // follows. Requiring no-space `typeof<` disambiguates from a variable
        // comparison `typeof < x` (which keeps `typeof` an identifier).
        type_keyword_expression: $ => seq(
            alias(
                token(seq(choice("sizeof", "typeof", "typedefof"), "<")),
                $.type_intrinsic,
            ),
            $.type_expression,
            ">",
        ),

        // `Map.empty<string, int>`  `f<int>` — explicit type-argument
        // application on a value/function. Same lexical shape as a `<` / `>`
        // comparison chain, so the declared `[type_application_expression,
        // _expression]` / `_simple_expression` conflicts let GLR explore both
        // and commit to this form when the `<…>` is actually closed by a
        // matching `>` enclosing type-expressions.
        //
        // SINGLE type-argument IS supported (`f<int>`, `f<'T>`) — the arg list
        // is `type_expression (, type_expression)*`, i.e. one-or-more, commas
        // optional. A plain comparison like `x < y` has no closing `>`, so the
        // binary reading wins; `x < y && y > z` likewise stays binary.
        //
        // Accepted tradeoff: because the lexer can't see whitespace, a spaced
        // `a < b > c` is read as generic application `a<b> c`, not `(a < b) > c`.
        // (Real F# disambiguates these by spacing; we can't, and generic
        // application is the more useful reading for a highlighter.)
        type_application_expression: $ => seq(
            $.long_identifier,
            "<",
            choice($.type_expression, $.nullable_type),
            repeat(seq(",", choice($.type_expression, $.nullable_type))),
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

        // `(+) 1 2`, `(=) (P.Id uid) y` — an operator NAME applied to arguments.
        // Kept a DEDICATED rule (rather than letting `operator_name` be a general
        // application head) so `application_expression` — and the SRTP member
        // constraints that share the heavily-overloaded `(` — are untouched.
        // `operator_name` is otherwise only reachable as an application argument
        // (via `_simple_expression`), so an applied operator had no parse.
        operator_application: $ => prec.left(PREC.APP_EXPR, seq(
            alias($._value_operator_name, $.operator_name),
            repeat1($._simple_expression),
        )),

        // Bare `(op)` as a first-class value in expression position. Negative
        // prec so `operator_application` (APP_EXPR) wins whenever an argument
        // follows; this only matches when the operator stands alone. Hidden so
        // the tree shows just `operator_name` (same node as the applied form).
        _operator_value: $ => prec(-1, alias($._value_operator_name, $.operator_name)),

        // Operator-name set for the APPLIED form. Excludes the bare `^` / `&` /
        // `|` (which collide with SRTP `(^T …)` / byref `(& …)` / anon-record
        // `(| …)` openers that also start with `(` + that char) — those are rare
        // as applied operators and not worth the parse ambiguity.
        _value_operator_name: $ => seq(
            "(",
            choice($.symbolic_op, "+", "-", "*", "/", "%", "=", "<", ">"),
            ")",
        ),

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
            // `Map.empty<string, int>` — generic type-argument application.
            $.type_application_expression,
            // `(+)`, `(>>=)`, `(!//!)` — operator as a first-class function value.
            // Used as application heads (`(+) 1 2`) or arguments (`x (!//!) y`).
            $.operator_name,
            // `(|Foo|)` / `Module.(|Foo|)` — active pattern as a value.
            $.active_pattern_expression,
            // `not` as a first-class function value (`not >> g`, `f not`).
            $.not_function,
            $.object_expression,
            // Accept `async { … }` / `task { … }` etc. as application arguments.
            // Without this, a body that sequences a CE after another statement
            // (`printfn "a"; async { … } |> ignore`) hits a parse error because
            // `{ return … }` doesn't fit as a record_expression. Including
            // computation_expression here lets the parser fall back to a chained
            // application (semantically wrong but free of ERROR) — the proper
            // fix is sequence-expression support, tracked separately.
            $.computation_expression,
            // `f <@ expr @>` — a code quotation as an application ARGUMENT. Without
            // this, `<@` after a value lexes as the `<@` symbolic_op (binary), so
            // `EvaluateQuotation <@ 42 @>` mis-parses. (Only the typed `<@ @>` form
            // is added here; `untyped_quotation`'s `<@@` ripples GLR states and
            // regresses Set.fs, and isn't used as an application arg in practice.)
            $.typed_quotation,
        ),

        // All infix operations in one rule (one rule keeps post-_expression state bloat down).
        // Each alternative carries its own prec to resolve shift/reduce between operators.
        // Each alternative exposes `left` / `operator` / `right` fields so the
        // operands and the operator are queryable (e.g. `(binary_expression
        // operator: _ @op)`). Fields are output metadata only — they don't
        // change the parse table.
        binary_expression: $ => choice(
            prec.left(PREC.PIPE_EXPR,      seq(field('left', $._expression), field('operator', choice("|>", "<|", ">>", "<<")), field('right', $._expression))),
            prec.left(PREC.BOOL_OR,        seq(field('left', $._expression), field('operator', "||"), field('right', $._expression))),
            prec.left(PREC.BOOL_AND,       seq(field('left', $._expression), field('operator', "&&"), field('right', $._expression))),
            prec.left(PREC.ADDITIVE,       seq(field('left', $._expression), field('operator', choice("+", "-")), field('right', $._expression))),
            prec.left(PREC.MULTIPLICATIVE, seq(field('left', $._expression), field('operator', choice("*", "/", "%")), field('right', $._expression))),
            prec.left(PREC.INFIX_OP,       seq(field('left', $._expression), field('operator', choice(">", "<", ">=", "<=", "=", "<>")), field('right', $._expression))),
            prec.right(PREC.INFIX_OP,      seq(field('left', $._expression), field('operator', "::"), field('right', $._expression))),
            prec.left(PREC.INFIX_OP,       seq(field('left', $._expression), field('operator', $.symbolic_op), field('right', $._expression))),
            prec.right(PREC.LARROW,        seq(field('left', $._expression), field('operator', "<-"), field('right', $._expression))),
            prec.right(1,                  seq(field('left', $._expression), field('operator', ".."), field('right', $._expression))),
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

        // `not` used as a first-class function value — it's an ordinary
        // FSharp.Core function, not a reserved operator: `not >> g`,
        // `xs |> List.map not`, `f not`. Low prec so `not x` still parses as
        // the prefix-application `unary_expression` above; this form only wins
        // when `not` can't take an operand (followed by a binary operator, or
        // sitting as a bare argument).
        not_function: _ => prec(-1, "not"),

        // Custom operators: `@` (list append) or any sequence of 2+ symbolic chars.
        // Restricted to 2+ chars; single-char operators like `+`/`-`/`*` are
        // tokens of their own elsewhere in the grammar. The char class covers
        // the full F# operator alphabet (op-char-first / op-char) so that
        // names like `(!//!)`, `(.//.)`, `(-//-)`, `(@//@)` parse as operators
        // rather than `//` being mis-tokenized as a line comment. Quotation
        // delimiters `<@`/`@>`/`<@@`/`@@>` are explicit string tokens — they
        // win over `symbolic_op` at the lexer.
        symbolic_op: _ => token(choice(
            "@",
            // `:=` (ref-cell assignment). `:` is NOT in the operator-char set
            // below (it would clash with type annotations), so list it here.
            ":=",
            /[!$%&*+\-.\/<=>?@^|~][!$%&*+\-.\/<=>?@^|~]+/,
        )),

        list_expression: $ => seq(
            "[",
            optional(choice(
                // Block form `[`⏎ elements ⏎`]`: `_bracket_open` captures the
                // element column, `_bracket_sep` separates newline-aligned
                // elements (a dedicated token a nested sequence can't absorb, so
                // elements never chain into one application), `_bracket_close`
                // pops at `]`.
                seq(
                    $._bracket_open,
                    $._expression,
                    repeat(prec(PREC.PAREN_EXPR, seq(choice(";", $._bracket_semi), $._expression))),
                    optional(choice(";", $._bracket_semi)),
                    $._bracket_close,
                ),
                // Inline form `[ a; b; c ]`. The `;` separator is given a static
                // precedence above SEQ_EXPR so it binds as an ELEMENT separator
                // here rather than extending the element into a
                // `sequence_expression` (`[ a; b ]` = two elements).
                seq(
                    $._expression,
                    repeat(prec(PREC.PAREN_EXPR, seq(";", $._expression))),
                ),
            )),
            "]",
        ),

        array_expression: $ => seq(
            "[|",
            optional(choice(
                // Block form `[|`⏎ elements ⏎`|]` (see list_expression).
                seq(
                    $._bracket_open,
                    $._expression,
                    repeat(prec(PREC.PAREN_EXPR, seq(choice(";", $._bracket_semi), $._expression))),
                    optional(choice(";", $._bracket_semi)),
                    $._bracket_close,
                ),
                // Inline form `[| a; b; c |]` (see list_expression).
                seq(
                    $._expression,
                    repeat(prec(PREC.PAREN_EXPR, seq(";", $._expression))),
                ),
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
                    field('base', choice($._simple_expression, $.bracket_index_expression, $.index_expression)),
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
                    // Base may be an application (`{ Foo.bar [] x with F = y }`)
                    // or a generic call (`{ Default<_>() with … }`), not only a
                    // simple value — disambiguated by `with` (the full
                    // expression is parsed before `with`).
                    field('base', choice($._simple_expression, $.application_expression, $.bracket_index_expression, $.index_expression)),
                    "with",
                    $._record_fields,
                ),
                // Block form with the base on its OWN line after `{`:
                //   { ⏎ base with ⏎ field … ⏎ }
                // The scanner emits `_body_indent` at the base's column (`{` had
                // no same-line content); without this branch only the no-base
                // field list consumes that indent, and `base with` errors.
                seq(
                    $._layout_open,
                    field('base', choice($._simple_expression, $.application_expression, $.bracket_index_expression, $.index_expression)),
                    "with",
                    $._record_fields,
                    $._layout_end,
                ),
                $._record_fields,
            ),
            "}",
        ),

        // Shared field-list body for record/anonymous-record expressions.
        // Uses the same indented-or-inline helper as `record_type_defn`.
        _record_fields: $ => indentedOrInlineFieldList($, $.record_field, PREC.APP_EXPR + 1, { sameLineBraceForm: true }),

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
        // Uniform model: every body is a layout (open at the body's first-token
        // column, close on dedent / mid-line closer). The old bare-`_expression`
        // alternative is gone — it created an ambiguity where an inline body could
        // reduce WITHOUT a layout close, so e.g. `if a then b`⏎`else …` reduced the
        // `if` before the `else` could attach.
        _indented_or_inline_body: $ => seq($._expr_open, $._expression, $._layout_end),

        if_expression: $ => prec.right(PREC.IF_EXPR, choice(
            prec(2, seq(
                "if",
                $._expression,
                "then",
                $._indented_or_inline_body,
                // `else if c then …` ≡ `elif c then …` (F#). The scanner suppresses
                // `_else_open` before `if`, so the final-else branch below can't take
                // `else if`; it flattens here, keeping the chain at one level (an
                // else-body-nested if would over-close at a later dedented `elif`).
                repeat(seq(choice("elif", seq("else", "if")), $._expression, "then", $._indented_or_inline_body)),
                // Final else: a non-`if` body (uses `_else_open`, which the scanner
                // declines before `if`).
                optional(seq("else", $._else_open, field('else', $._expression), $._layout_end)),
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
                "$", "~", "!",   // single-char custom operators (`($)`, `(~)`, `(!)`)
            ),
            ")",
        ),

        // `(|Name|)` or `Module.Path.(|Name|)` — an active-pattern function used
        // as a first-class value (`Seq.sumBy (M.(|Foo|))`, `snd >> M.(|Foo|) >> g`).
        // The qualified tail `.(|Name|)` is ONE token (`active_pattern_member`) so
        // the lexer munches it as a unit — it never competes with a
        // `long_identifier`'s own `.` (a plain `.Sub` has no `(|`), which keeps
        // ordinary dotted access (`obj.A.B`) conflict-free.
        active_pattern_expression: $ => choice(
            $.active_pattern_name,
            seq($.long_identifier, $.active_pattern_member),
        ),

        // The qualified-active-pattern tail: `.(|Even|Odd|)` etc. Same shape as
        // `active_pattern_name` with a leading `.`, kept a single token on purpose.
        active_pattern_member: _ => token(seq(
            ".",
            "(|",
            /[a-zA-Z_][a-zA-Z0-9_']*/,
            repeat(seq("|", /[a-zA-Z_][a-zA-Z0-9_']*/)),
            optional(seq("|", "_")),
            "|)",
        )),

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
            // `let private foo = ...` / `let public foo = ...` /
            // `let internal foo = ...`. Without this, `private` would be
            // parsed as the binding name and `foo` as a parameter — which
            // already cascades into ERROR nodes downstream when the body
            // is a tuple or the name pattern uses comma-separated bindings.
            optional($.access_modifier),
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
        // Attributes / doc comments may appear EITHER before `let` or between
        // `let [rec]` and the binding name. Both forms are equivalent:
        //   [<Literal>] let X = 11
        //   let [<Literal>] X = 11
        let_binding: ($) => prec.right(PREC.LET_DECL, choice(
            prec(2, prec.dynamic(1, seq(
                decoration($),
                optional("static"),
                "let",
                optional(token.immediate("!")),
                optional("rec"),
                decoration($),
                $._let_signature,
                "=",
                // Uniform: the body is a layout (opens at the body's first-token
                // column, closes on dedent / mid-line `in` / closer). Covers
                // inline `let x = e`, own-line bodies, and `let x = e in …` (the
                // scanner's `in` ender closes the body before `in`).
                seq($._layout_open, field('body', $._expression), $._layout_end),
                repeat($.let_and_binding),
            ))),
            prec(1, prec.dynamic(1, seq(
                decoration($),
                optional("static"),
                "let",
                optional(token.immediate("!")),
                optional("rec"),
                decoration($),
                $._let_signature,
                "=",
                repeat($.let_and_binding),
            ))),
        )),

        // and name params [: type] = expr  (mutual recursion continuation)
        // Body uses the SAME offside wrappers as `let_binding` (`_body_indent` /
        // `_let_body_open`) — NOT a bare `_expression`. Without the wrapper a
        // `function` / `match` body whose arms sit at the declaration column
        // absorbs the FOLLOWING sibling `let` declaration into the and-binding's
        // body as a `let_expression` continuation (wrong, and an error when that
        // `let` has no continuation of its own).
        let_and_binding: ($) => prec.right(PREC.LET_DECL, choice(
            prec(2, seq("and", optional(token.immediate("!")), $._let_signature, "=",
                seq($._layout_open, field('body', $._expression), $._layout_end),
            )),
            prec(1, seq("and", optional(token.immediate("!")), $._let_signature, "=")),
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
            optional(token.immediate("!")),
            optional("rec"),
            $._let_signature,
            "=",
            // S_EXPR body (`_expr_open`) so a `let x = e in body` closes the value
            // `e` at the inline `in`.
            seq($._expr_open, field('body', $._expression), $._layout_end),
            // `let rec f = … and g = … and h = …` — mutual recursion in a NESTED
            // (expression-position) let, same as the top-level `let_binding`.
            // Uses `_and_decl_indented` (NOT the top-level `let_and_binding`) so
            // each `and` body carries the same `_indent`/`_inline` offside wrapper
            // as the main body — otherwise the bare-`_expression` and-body absorbs
            // the let_expression's continuation line into a sequence. Without any
            // of this the `and …` lines parse as a bogus application (`and` lexed
            // as an identifier) and lose their keyword highlight.
            repeat(alias($._and_decl_indented, $.let_and_binding)),
        ),

        _and_decl_indented: ($) => seq(
            "and",
            optional(token.immediate("!")),
            $._let_signature,
            "=",
            choice(
                seq($._layout_open, field('body', $._expression), $._layout_end),
                seq($._layout_open, field('body', $._expression), $._layout_end),
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
            seq("use", optional(token.immediate("!")), field('name', $.identifier), "=", $._expression),
        ),

        // `expr.Member` — member access ONLY when the LHS isn't a pure-identifier
        // chain. Pure-identifier chains (`A.B.C.D`) are owned exclusively by
        // `long_identifier`, so the parser can't ambiguously pick
        // `dot_expression(long_identifier(A, B, C), D)` for them. The object
        // field is restricted to compound expressions (parenthesized, index,
        // application, typed, struct_tuple, or a nested dot_expression for
        // further chaining off a compound LHS).
        dot_expression: $ => prec(PREC.DOT, seq(
            field('object', $._dot_object),
            ".",
            field('member', $.identifier),
        )),

        // `obj?member` / `obj?(expr)` — F#'s dynamic-lookup operator (`(?)`), used
        // heavily in Fable JS interop (`el?style`, `path?join(a, b)`). The `?` is
        // `token.immediate` (no space before it) so it's distinct from an
        // `optional_named_arg` argument (`f ?name = x`, which has a space).
        dynamic_expression: $ => prec(PREC.DOT, seq(
            field('object', $._expression),
            token.immediate("?"),
            field('member', choice($.identifier, $.parenthesized_expression)),
        )),

        // Compound LHS allowed as the object of a `dot_expression`. Notably
        // excludes long_identifier/identifier so pure-identifier chains stay
        // in `long_identifier` and never split into nested dot_expressions.
        _dot_object: $ => choice(
            $.parenthesized_expression,
            $.typed_expression,
            $.struct_tuple_expression,
            $.index_expression,
            $.bracket_index_expression,
            $.application_expression,
            // `[1; 2].GetHashCode()` / `[|1; 2|].Length` — member access on a
            // list / array / record literal.
            $.list_expression,
            $.array_expression,
            $.record_expression,
            $.anonymous_record_expression,
            // `Type<'T>.StaticMember` / `Type<int>.Member` — static-member (or
            // nested-type) access on a generic type name. Without this, the
            // type_application_expression isn't a valid member-access object, so
            // the parser only limps through via a MISSING `not` recovery (no
            // clean tree) or errors outright inside a type augmentation body.
            $.type_application_expression,
            // `typeof<int>.Name` / `typeof<_>.IsGenericType` — member access on a
            // type-level intrinsic (common in reflection code).
            $.type_keyword_expression,
            $.dot_expression,
            $.begin_end_expression,
            // `'T.StaticMember` / `^T.StaticMember` — modern F# SRTP member access,
            // where a (statically-resolved) type parameter is the root of a member
            // chain, e.g. `'a.suffixFormat.SuffixDelimStart`.
            $.type_parameter,
            // `"abc".Length` / `'a'.ToString()` / `42 .ToString()` — member access on
            // a literal (a `42.ToString()` with no space is a float by F#'s rule).
            $._literal,
        ),

        // arr.[0]  arr.[1..2]  arr.[..2]  arr.[1..]  dict.["k"]  m.[0, 1]
        // `.[` is a single terminal so it never conflicts with the `.` in long_identifier.
        index_expression: $ => prec(PREC.INDEX_EXPR, seq(
            field('object', $._expression),
            ".[",
            field('index', $._index_args),
            "]",
        )),

        // arr[0]  arr[1..2]  m.Value[..2]  dict["k"]  (F# 6+ dotless indexer).
        // `token.immediate("[")` requires NO space before `[`, distinguishing an
        // index (`arr[i]`) from application to a list literal (`f [i]`).
        bracket_index_expression: $ => prec(PREC.INDEX_EXPR, seq(
            field('object', $._expression),
            token.immediate("["),
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

        // SRTP call-site:
        //   ( ^T : (member X: int) a )
        //   ( ^T : (static member (+): ^T * ^T -> ^T) (a, b) )
        // The outer parens delimit the whole expression. Inside, a type
        // parameter is followed by an SRTP member signature (same shape as
        // the SRTP `type_constraint` form) and one argument expression
        // (which can itself be a parenthesized tuple for static / multi-arg
        // calls). prec(PAREN_EXPR) so it wins over `parenthesized_expression`
        // when the first child is a `^T`-shaped `type_parameter`.
        srtp_call_expression: $ => prec(PREC.PAREN_EXPR, choice(
            // Single-parameter SRTP call: `(^T : (member …) arg)`
            seq(
                "(",
                $.type_parameter,
                ":",
                "(",
                optional("static"),
                "member",
                field('member_name', choice($.identifier, $.operator_name)),
                ":",
                field('member_type', $.type_expression),
                ")",
                field('argument', $._expression),
                ")",
            ),
            // Heterogeneous SRTP call: `((^a or ^b) : (member …) arg)`
            // The LHS is its own parenthesised list; each term is either a
            // type_parameter or a concrete type identifier, joined by `or`.
            seq(
                "(",
                "(",
                choice($.type_parameter, $.long_identifier),
                repeat1(seq("or", choice($.type_parameter, $.long_identifier))),
                ")",
                ":",
                "(",
                optional("static"),
                "member",
                field('member_name', choice($.identifier, $.operator_name)),
                ":",
                field('member_type', $.type_expression),
                ")",
                field('argument', $._expression),
                ")",
            ),
        )),

        // nameof expr  — returns the string name of the identifier/member at compile time
        nameof_expression: $ => seq("nameof", $._simple_expression),

        // new TypeName(args)  or  new TypeName<T>(args)
        // Type is restricted to long_identifier/generic_type so the `(` can't be
        // consumed as a parenthesized_type.
        new_expression: $ => prec(PREC.NEW_OBJ,
            seq(
                "new",
                // `new 'T()` — construct a generic type parameter (used with a
                // `'T: (new: unit -> 'T)` constraint).
                choice($.generic_type, $.long_identifier, $.type_parameter),
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
            decoration($),
            "exception",
            field('name', $.identifier),
            optional(seq("of", $.type_expression)),
        )),

        // Arm list shared by match / try-with / function. `_match_open` pushes a
        // MATCH layout context at the first arm's column (emitted by the scanner
        // right after `with`/`function`); `_match_end` closes the list when a line
        // dedents below the arm column (or sits at it without a leading `|`). The
        // leading `|` of each arm is `match_arm`'s own `optional("|")`.
        _match_arms: $ => prec.right(seq(
            $._match_open,
            $.match_arm,
            repeat($.match_arm),
            $._match_end,
        )),

        // The try body uses a DEDICATED layout sort (`_try_open` → S_TRY) so a
        // multi-statement body sequences (e.g. inside a CE: `async { try do! a⏎
        // return! b with … }`) and closes at `with`/`finally`. A dedicated sort
        // (not the generic S_EXPR) means the `with`-close is try-specific and does
        // NOT fire for a `match … with` sitting inside an enclosing expr body.
        try_expression: $ => prec.right(PREC.MATCH_EXPR, seq(
            "try",
            seq($._try_open, $._expression, $._layout_end),
            choice(
                seq("with", $._match_arms),
                seq("finally", seq($._try_open, $._expression, $._layout_end)),
            ),
        )),

        // lazy expr / assert expr — prefix keyword wrapping an expression
        prefix_keyword_expression: $ => prec(PREC.PREFIX_EXPR,
            seq(choice("lazy", "assert"), $._expression),
        ),

        // `do expr` — a unit-returning statement written explicitly inside a
        // sequence (`do v := 0`, `do obj.Mutate ()`). prec.right at 2 (just above
        // SEQ_EXPR=1) so it reduces as ONE statement and the enclosing sequence
        // keeps the following lines as siblings — while the operand still grabs a
        // full application / assignment (prec ≥ 4). (A `|>`/tuple operand, prec 1,
        // would bind outside the `do`, but that form is degenerate for `do`.)
        do_expression: $ => prec.right(2, seq("do", $._expression)),

        // begin expr end  — sequenced block (equivalent to parenthesized)
        begin_end_expression: $ => prec(PREC.PAREN_EXPR, seq("begin", $._expression, "end")),

        // function | pat -> expr …  — shorthand for fun x -> match x with
        function_expression: $ => prec.right(PREC.MATCH_EXPR,
            seq("function", $._match_arms),
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
        // Trade-off documented in LIMITATIONS.md: a non-CE `for` with a
        // multi-statement body parses as a single chained application instead
        // of `sequence_expression`. Attempted fix via a separate
        // `_ce_for_clause` rule (aliased to `for_expression`) didn't work —
        // tree-sitter prefers the longest match, so even with high precedence
        // on the body-less form, the body-present form wins when both can
        // match. See the LIMITATIONS.md entry for the next thing to try.
        // Body shape (`_for_body`): a real loop body uses `_for_body_open` /
        // `_for_body_close` (scanner-emitted) so the body column is pushed onto
        // the indent stack and `_virtual_semi` sequences multi-statement
        // bodies. The scanner does NOT emit `_for_body_open` when the next
        // significant token is a query-CE operator (`where`/`select`/… or a
        // chained `for`), so the `query { for x in xs do where … select … }`
        // form keeps the for body EMPTY and the operators stay `query_operator`
        // siblings. The bare `optional($._expression)` fallback covers that
        // empty-body CE case, inline single-line bodies, and mid-edit. Inlined
        // (not a named rule) so the empty alternative only appears inside the
        // non-empty `for … do` sequence.
        for_expression: $ => prec.right(PREC.IF_EXPR, seq(
            "for",
            choice(
                seq(
                    choice($.identifier, $.wildcard_pattern, $.tuple_pattern, $.record_pattern,
                        // `for item, text in pairs do …` — unparenthesised tuple binder.
                        $.unparenthesized_tuple_pattern),
                    "in", $._expression,
                    choice(
                        seq("do",
                            // `_for_open` is suppressed when the "body" sits at the
                            // enclosing (CE) column rather than indented — i.e. a
                            // query `for x in xs do`⏎`where …`/`select …` — so the
                            // body stays empty and the operators are query_operator
                            // CE siblings. A real indented/inline loop body opens
                            // normally. (No bare `optional($._expression)` fallback:
                            // it would greedily eat the next query operator.)
                            optional(seq($._for_open, field('body', $._expression), $._layout_end)),
                        ),
                        // `[ for x in xs -> expr ]` — list/seq/array comprehension
                        // yield shorthand (sugar for `do yield expr`). The `->`
                        // belongs to the for, not the enumerable: `prec.dynamic`
                        // biases the parser to end the `in` expression and take
                        // this arm rather than read `->` as a `symbolic_op`
                        // extending the enumerable into a bogus binary_expression.
                        prec.dynamic(1, seq("->", field('body', $._expression))),
                    ),
                ),
                seq(
                    // `_` is a valid range-loop binder (`for _ = 0 to n do …`).
                    choice($.identifier, $.wildcard_pattern),
                    "=", $._expression, choice("to", "downto"), $._expression, "do",
                    optional(seq($._for_open, field('body', $._expression), $._layout_end)),
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
                optional(choice(
                    // Multi-line `builder {`⏎ statements ⏎`}`. `reserved('query_ce')`
                    // wraps only the STATEMENTS (so query operators activate) and not
                    // the external `_ce_body_*`/`_ce_sep` tokens. `_ce_sep` is the
                    // dedicated separator (a binding's trailing expression can't steal
                    // it); `_ce_body_close` pops at `}`.
                    seq(
                        $._bracket_open,
                        reserved('query_ce', $._ce_statement),
                        repeat(seq(choice(";", $._bracket_semi), reserved('query_ce', $._ce_statement))),
                        optional(choice(";", $._bracket_semi)),
                        $._bracket_close,
                    ),
                    // Single line: `builder { return x }` / `seq { x; y }`.
                    seq(
                        reserved('query_ce', $._ce_statement),
                        repeat(seq(";", reserved('query_ce', $._ce_statement))),
                    ),
                )),
                "}",
            ),
        ),

        // Statements inside a CE body. `_expression` at the end covers
        // return/yield/return!/yield!/do!/for/while/if/match/etc.
        // The query_* alternatives only match in CE bodies because their leading
        // keywords are in the `query_ce` reserved set, which is activated by the
        // `reserved('query_ce', …)` wrap in `computation_expression`.
        _ce_statement: $ => choice(
            $.use_binding,
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
        // use x = disposable  (also used as a top-level _token outside CEs)
        use_binding: $ => prec.right(PREC.LET_DECL,
            seq("use", optional(token.immediate("!")), field('name', $.identifier), "=", $._expression),
        ),

        // match! expr with | pat -> expr …
        ce_match_bang_expr: $ => prec.right(PREC.MATCH_EXPR,
            seq("match!", $._expression, "with", $._match_arms),
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
                $._match_arms,
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
            field('body', $._match_arm_body),
        ),

        // Body of a match/try/function arm. Three shapes:
        //   1. Own line, indented: `_body_indent` pushes the body column so
        //      `_virtual_semi` sequences multi-statement bodies and a
        //      dedented trailing statement closes the arm.
        //   2. Inline (`| pat -> body`): `_match_body_open` captures the
        //      enclosing indent (≈ the `match` column) and `_match_body_close`
        //      fires when the next line returns to or below it — keeps a
        //      trailing dedented statement (e.g. a final `0` at the `match`
        //      column) out of the last arm's body.
        //   3. Plain `_expression` fallback (single-line arms, EOF, mid-edit).
        _match_arm_body: $ => seq($._layout_open, $._expression, $._layout_end),

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

        // pat1 & pat2  — conjunction (AND) pattern, e.g. `Foo x & Bar y` /
        // `ForFile f & HasEditIn r`. Both subpatterns must match.
        and_pattern: $ => prec.left(1, seq($.pattern, "&", $.pattern)),

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

        // `Some x` / `Foo.Bar x` (constructor) and `Contains keys value`
        // (parameterised active pattern) — a long_identifier applied to one or
        // more ARGUMENT patterns. Arguments are `_tuple_elem_pattern` (atomic
        // forms: `_`, literals, `[…]`, `[|…|]`, `{…}`, `(…)`, a bare
        // long_identifier) rather than the full `$.pattern`, so multiple args
        // stay siblings (`Contains [a] [b]` → two list args) instead of the
        // first arg greedily swallowing the rest as a nested application.
        identifier_pattern: $ => choice(
            $.long_identifier,
            prec.right(1, seq($.long_identifier, repeat1($._tuple_elem_pattern))),
        ),

        // Inside a parenthesised tuple pattern, an element AFTER the first may
        // carry a type annotation WITHOUT its own parens — the tuple's parens
        // suffice:  (Key3(_, _, r: RevUtc), s: State)   (a, b: int).
        // The first element stays a plain pattern so the single-element forms
        // `(Some x)` / `(x: T)` keep matching tuple_pattern / typed_pattern
        // unambiguously (no comma → no typed-item branch).
        tuple_pattern: $ => seq(
            "(",
            repeat($.attribute),           // `([<Attr>] x, …)` — attr on a param element
            optional("?"),                 // OOP optional param: (msg, ?range)
            $.pattern,
            repeat(seq(",", $._tuple_pattern_item)),
            ")",
        ),

        // Like tuple_pattern but with a typed first element (`(x: T, y)`).
        // Kept separate from tuple_pattern — and reachable only from $.pattern,
        // never from `parameter` — so OOP-style `tuple_params` keeps owning the
        // same shape in parameter position. The required comma (repeat1) means a
        // lone `(x: T)` still parses as typed_pattern, not a 1-tuple.
        tuple_typed_first_pattern: $ => seq(
            "(",
            $.tuple_typed_pattern,
            repeat1(seq(",", $._tuple_pattern_item)),
            ")",
        ),

        _tuple_pattern_item: $ => seq(
            repeat($.attribute),           // `(x, [<Attr>] y)` — attr on a param element
            optional("?"),                 // OOP optional param: (msg, ?range)
            choice(
                $.pattern,
                $.tuple_typed_pattern,
            ),
        ),

        // `name : type` element of a parenthesised tuple pattern (the tuple's
        // own parens stand in for the per-element parens `typed_pattern` needs).
        tuple_typed_pattern: $ => seq(
            field('pattern', choice($.long_identifier, $.wildcard_pattern)),
            ":",
            field('type', $.type_expression),
        ),

        // Elements of unparenthesized_tuple_pattern. Excludes identifier_pattern's
        // constructor-application form which would otherwise consume `add a b` in
        // `let add a b = ...` before the parser sees there's no `,`.
        _tuple_elem_pattern: $ => choice(
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
            // The optional `when` clause covers an inline constraint on the
            // param's type variable: `(value: 'T when 'T: null)`.
            // The trailing constraint covers `(value: 'T when 'T: null)` and the
            // subtype form `(resource: 'T :> IDisposable)`.
            prec(20, seq("(", repeat($.attribute), $.identifier, ":", choice($.type_expression, $.nullable_type), optional(choice($._when_constraints, seq(":>", $.type_expression))), ")")),
            prec(20, seq("(", repeat($.attribute), $.identifier, ")")),
            // `?loc` — bare (un-parenthesized) curried optional param. A type
            // annotation needs parens (`(?loc: int)`) so `?loc : T` reads `T` as
            // the return type, not the param type.
            prec(20, seq("?", $.identifier)),
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
            // `#IDisposable` / `#seq<'T>` — flexible type (this type or a subtype).
            $.flexible_type,
            // `name: T` / `?name: T` element of a member/abstract signature.
            $.labelled_type,
        ),

        // Flexible (subtype) constraint: `#BaseType`. Binds tightly to the
        // following type so `#A -> B` is `(#A) -> B`. The constituent type is a
        // concrete head (identifier/generic/postfix/array) rather than a full
        // type_expression, so `#` doesn't swallow a trailing `->`/`*`.
        flexible_type: $ => prec(TYPE_PREC.APP, seq(
            "#",
            choice($.long_identifier, $.generic_type, $.postfix_type, $.array_type),
        )),

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

        // list<int>, Map<string, int>, ResizeArray<string | null>
        // A `nullable_type` (`string | null`) is allowed as a type argument: inside
        // `<…>` the `|` is unambiguous (arguments are delimited by `,` and `>`, no
        // union case can follow), the same reasoning that lets `parenthesized_type`
        // carry one.
        generic_type: $ => prec(TYPE_PREC.APP, seq(
            $.long_identifier,
            "<",
            choice(prec.dynamic(1, $.type_expression), $.nullable_type, $.measure_expression),
            repeat(seq(",", choice(prec.dynamic(1, $.type_expression), $.nullable_type, $.measure_expression))),
            ">",
        )),

        // int[]
        array_type: $ => prec(TYPE_PREC.APP, seq(
            $.type_expression,
            "[",
            "]",
        )),

        // (int -> string)  /  (string | null)
        parenthesized_type: $ => seq("(", choice($.type_expression, $.nullable_type), ")"),

        // `string | null` — F# 9 nullable reference type. Deliberately NOT a
        // member of the general `type_expression` choice: its `|` would clash with
        // the union-case separator. Instead it's allowed only in unambiguous
        // annotation positions (parenthesised types, parameter / return / field
        // type annotations), where no union `|` can follow. Operand is an atomic
        // type head (incl. a parenthesised type, so `(string list) | null` works).
        nullable_type: $ => seq(
            choice($.long_identifier, $.generic_type, $.postfix_type, $.array_type,
                $.parenthesized_type, $.type_parameter),
            "|",
            "null",
        ),

        // 'a  ^T  '``Generic type with spaces``
        // Backtick-quoted identifier form is also accepted so that type
        // parameters with spaces or other non-word chars parse.
        // `prec(-1)`: a `'a'` lexes as BOTH a type_parameter (ident `a'`) and a
        // char literal — same length — and now that type_parameter is valid in
        // expression position (SRTP `'T.Member`), they compete. Lower precedence
        // lets `char_literal` win for `'a'`; `'a` (no closing quote, e.g. `'a.X`)
        // still lexes as a type_parameter since char needs the closing `'`.
        type_parameter: _ => token(prec(-1, seq(
            choice("'", "^"),
            choice(
                /[a-zA-Z_][a-zA-Z0-9_']*/,
                /``[^`\n\r\t]+``/,
            ),
        ))),

        // ML-style prefix params on a type definition: `'T` or `('a, 'b)`,
        // appearing before the type name (`type 'T set`, `type ('a,'b) pair`).
        prefix_type_parameters: $ => choice(
            $.type_parameter,
            seq("(", $.type_parameter, repeat(seq(",", $.type_parameter)), ")"),
        ),

        // `when 'T :> IFoo and 'U : comparison` — generic-constraint clause,
        // shared by `type_parameter_list` (inside `<…>`) and `type_decl` /
        // `type_and_decl` (outside, between `<…>` and `=`).
        _when_constraints: $ => prec.right(seq(
            "when",
            $.type_constraint,
            repeat(seq("and", $.type_constraint)),
        )),

        // <'T, 'U when 'T :> IFoo and 'U : comparison>
        // A type parameter may carry an attribute (`<[<Measure>] 'u>`,
        // `<[<EqualityConditionalOn>] 'T>`).
        type_parameter_list: $ => seq(
            "<",
            repeat($.attribute), $.type_parameter,
            repeat(seq(",", repeat($.attribute), $.type_parameter)),
            optional($._when_constraints),
            ">",
        ),

        // 'T :> IFoo   'T : null   'T : comparison   …
        type_constraint: $ => choice(
            seq($.type_parameter, ":>", $.type_expression),
            seq($.type_parameter, ":", "null"),
            seq($.type_parameter, ":", "not", "null"),   // F# 9 non-null constraint
            seq($.type_parameter, ":", "struct"),
            seq($.type_parameter, ":", "not", "struct"),
            seq($.type_parameter, ":", "comparison"),
            seq($.type_parameter, ":", "equality"),
            seq($.type_parameter, ":", "unmanaged"),
            seq($.type_parameter, ":", "enum", "<", $.type_expression, ">"),
            seq($.type_parameter, ":", "delegate", "<", $.type_expression, ",", $.type_expression, ">"),
            // Constructor constraint: `^T : (new: unit -> 'T)`. The `unit -> 'T`
            // is a function type_expression so `unit` is a real (colourable) type.
            seq($.type_parameter, ":", "(", "new", ":", $.type_expression, ")"),
            // SRTP member constraint: ^T : (member Foo: int -> int)
            //                         ^T : (static member (+): ^T * ^T -> ^T)
            seq($.type_parameter, ":", "(",
                optional("static"),
                "member",
                field('member_name', choice($.identifier, $.operator_name)),
                ":",
                field('member_type', $.type_expression),
                ")"),
            // Heterogeneous SRTP member constraint:
            //   (^a or ^b) : (static member fmap: (^c -> ^d) * ^b -> ^e)
            //   (CFunctor or ^b) : (static member replace: ^a * ^b -> ^c)
            // Each LHS term is either a type_parameter (`^a`) or a concrete
            // type identifier (`CFunctor`), joined by `or`.
            seq(
                "(",
                choice($.type_parameter, $.long_identifier),
                repeat1(seq("or", choice($.type_parameter, $.long_identifier))),
                ")",
                ":",
                "(",
                optional("static"),
                "member",
                field('member_name', choice($.identifier, $.operator_name)),
                ":",
                field('member_type', $.type_expression),
                ")",
            ),
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

            // `#nowarn`/`#load`/`#r`/… non-conditional directives. The conditional
            // ones (`#if`/`#elif`/`#else`/`#endif`) are `extras` now, so they're not
            // listed here.
            $.preproc_directive,

            // Scope declarations
            $.namespace_decl,
            $.module_decl,
            $.import_decl,         // `open Foo`

            // Type-level declarations
            $.type_decl,
            $.type_extension,
            $.exception_decl,

            // Value-level declarations not shared with class bodies
            $.use_binding,

            // attribute + let_binding + do_stmt + the four comment forms
            // (shared with `_class_body_member` via `_decl_or_comment`).
            $._decl_or_comment,

            // Bare expression statements (last so all the above forms win
            // when their leading keyword/punctuation is unambiguous)
            $._expression,
        ),

        // Plain identifiers and `` `any text` ``-quoted form, unified in one terminal.
        // `word: $.identifier` still drives keyword detection; backtick forms never
        // match keywords because the regex requires the backticks.
        // F# identifiers follow the Unicode rules: first char is any Unicode
        // letter (categories Lu/Ll/Lt/Lm/Lo/Nl) or underscore; subsequent
        // chars add Unicode digits (Nd) and apostrophe. Using \p{L} / \p{Nd}
        // is a good practical approximation that covers `π`, `accentué`,
        // `café`, `数学`, etc. without enumerating script ranges by hand.
        // The backtick form `` `…` `` accepts almost anything between the
        // delimiters and is unchanged.
        identifier: _ => token(choice(
            /[\p{L}_][\p{L}\p{Nd}_']*/,
            /``[^`\n\r\t]+``/,
        )),

        // Pure-identifier chains of 1–2 segments stay as one long_identifier.
        // Chains of 3+ segments parse as nested `dot_expression(long_identifier(a,b),c)`,
        // which is irregular but currently unfixable with tree-sitter's LR
        // generator — see LIMITATIONS.md. All highlight rules and textobjects
        // are written to handle both forms.
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
        // `lf` (`0x…lf`, hex-bits-to-float32) and `LF` (`0x…LF`,
        // hex-bits-to-float64) are listed FIRST so tree-sitter's longest-match
        // wins over the single-char `l` / `L` alternatives.
        _int_suffix: _ => token.immediate(choice("lf", "LF", "uy", "us", "uL", "UL", "Ul", "ul", "un", "u", "y", "s", "l", "L", "n", "I", "m", "M", "f", "F")),
        _float_suffix: _ => token.immediate(choice("f", "F", "m", "M")),

        int_literal: $ => seq(
            choice($._hex, $._oct, $._bin, $._int),
            optional($._int_suffix),
        ),

        // Three forms (must keep `1..10` lexing as `int + .. + int`, not
        // `float(1.) + . + int(10)`):
        //   `digits . digits [exp]`   — `1.23`, `1.23e5`, `3.14f`
        //   `digits . exp`            — `180.e5`, `180.e5f` (no digits after `.`)
        //   `digits exp`              — `180e5`
        // The `digits . exp` form lets the `[eE][+-]?[0-9]+` after the dot
        // disambiguate from `..` (which has no `e`/`E` next).
        float_literal: $ => seq(
            choice(
                token(seq(/[0-9][0-9_]*/, ".", /[0-9][0-9_]*/, optional(seq(/[eE]/, optional(/[+-]/), /[0-9]+/)))),
                token(seq(/[0-9][0-9_]*/, ".", /[eE]/, optional(/[+-]/), /[0-9]+/)),
                token(seq(/[0-9]+/, /[eE]/, optional(/[+-]/), /[0-9]+/)),
                $._float_trailing_dot,
            ),
            optional($._float_suffix),
        ),

        // Char literal: `'x'`, escape sequences, plus optional `B` byte
        // suffix (`'F'B` produces a `byte` value). Same suffix-as-immediate-
        // token pattern as `string_literal` / `_string_byte_suffix`.
        char_literal: $ => seq($._char_content, optional($._char_byte_suffix)),

        _char_content: _ => token(
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
                        // Fallback: any other char after `\`. Real F# rejects
                        // unknown escape sequences (FS1157), but a syntax-
                        // highlighting grammar should still parse the string
                        // so a single bad escape doesn't cascade an ERROR
                        // through the rest of the file. The longer specific
                        // alternatives above still win for valid escapes.
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

        // _interp_string_text / _interp_verbatim_text / _interp_triple_text are
        // EXTERNAL tokens (see externals block + scan_interp_text in scanner.c) so
        // a leading `//` in the text isn't preempted by the line_comment extra.

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

        // `()` or `( )` (any amount of horizontal whitespace) — F# treats
        // both as the unit literal. token() with a regex so whitespace
        // INSIDE the literal is part of the token rather than being
        // absorbed as `extras`.
        unit: _ => token(/\([ \t]*\)/),

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

        // The `#if`/`#elif` condition — the rest of the directive line as a SINGLE
        // token. It must be one atomic token: `preproc_if` is an `extra`, extras must
        // have an unambiguous ending, and any structured (multi-token) condition —
        // even a flat `A || B || …` — fails that check. So the operators/symbols
        // inside the condition cannot be sub-coloured; the whole condition is neutral.
        preproc_expression: _ => token(/[^\n\r]+/),

        // Conditional-compilation directives. Modelled as standalone, body-LESS
        // nodes and added to `extras` (like comments) so they can appear in ANY
        // context — between list/array elements, CE statements, record fields, or
        // top-level decls — without breaking the surrounding parse. The content
        // they guard is just parsed in place (both `#if` and `#else` branches as
        // siblings); for a highlighting grammar that's exactly what we want, and it
        // avoids the cascade of errors a body-wrapping form caused inside `[ … ]`.
        preproc_if: $ => seq($.preproc_if_kw, field('condition', $.preproc_expression)),
        preproc_elif: $ => seq($.preproc_elif_kw, field('condition', $.preproc_expression)),
    }
});
