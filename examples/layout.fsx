// ============================================================================
// layout.fsx — offside/layout test harness, ordered simple → complex.
//
// Built for the uniform-layout scanner rewrite (see LAYOUT_REWRITE.md): each
// module isolates ONE layout construct so it can be validated construct-by-
// construct as the new model grows. Sub-modules keep type/value names from
// colliding. Every module must parse with ZERO errors.
//
// Sections are grouped by the layout SORT they exercise (Decl / Then / Do /
// Match / Let / Brackets) plus the accumulated-state regressions at the end.
// ============================================================================

// ── Decl: literals & inline let bodies ──────────────────────────────────────
module L01_InlineLet =
    let int_v = 1
    let float_v = 3.14
    let string_v = "hello"
    let bool_v = true
    let unit_v = ()
    let tuple_v = 1, 2
    let paren_v = (1 + 2) * 3

    // Destructuring let with a type-annotated FIRST tuple element.
    let (typed_a: int, typed_b: string) = pair

    // `\` at end of line continues the string (newline + leading ws elided).
    let continued = "the quick brown fox \
                     jumps over the lazy dog"

// ── Decl: own-line let bodies ───────────────────────────────────────────────
module L02_OwnLineLet =
    let single =
        42

    let arithmetic =
        1 + 2 + 3

    let piped =
        [ 1; 2; 3 ]
        |> List.map (fun x -> x + 1)
        |> List.sum

// ── Decl: functions ─────────────────────────────────────────────────────────
module L03_Functions =
    let add a b = a + b
    let typed (a: int) (b: int) : int = a + b
    // Inline `when` constraint on a parameter's type variable.
    let isDefault (value: 'T when 'T: equality) = value = Unchecked.defaultof<'T>
    // F# 9 non-null constraint.
    let firstKey<'K when 'K: not null> (m: Map<'K, int>) = Map.toList m |> List.head |> fst
    // IWSAM / bare-interface constraint (F# 7+): `'T` must implement IParsable<'T>.
    let parseIt<'T when IParsable<'T>> (s: string) = 's'
    // Subtype (`:>`) constraint on a parameter's type.
    let useDisposable (r: 'T :> System.IDisposable) = r.Dispose ()
    // Single-case union deconstruction in a typed parameter.
    let unwrap (Url url: Url) = url
    // Wildcard `_` in a tuple parameter (OOP-style multi-arg).
    let ignoreFirst (_: int, x: string) = x
    // A non-first tuple-param element may carry redundant parens (here around a
    // function-typed param): `(x, (f: A -> B), y)`.
    let parenElem (x: int, (pc: int -> int option), y: int) = x
    let curriedOwnLine a b =
        a * b

    // Member access directly on a literal.
    let litLen = "abc".Length
    let litChars = "bcd".ToCharArray()
    let charStr = 'x'.ToString()
    let quoteChar = '''   // unescaped single-quote char literal

    // Operator as a first-class value, and member access on a type intrinsic.
    let add = (+)
    let combine = List.reduce (+)
    let typeName = typeof<int>.Name

    // Member access on a collection / record literal.
    let listLen = [ 1; 2; 3 ].Length
    let arrHash = [| 1; 2 |].GetHashCode()
    let recHash = {| a = 1; b = 2 |}.GetHashCode()

    // Single-character custom operator definition.
    let ($) (f: obj) (x: obj) : obj = f

    // Code quotation as an application argument (`f <@ … @>`).
    let evalq = eval <@ 1 + 2 @>

    // Inline IL intrinsic `(# "opcode" args : type #)`.
    let inline retype (x: 'a) : 'b = (# "" x : 'b #)

    // Attribute with a target specifier (`return:`).
    [<return: Struct>]
    let (|Even|_|) n = if n % 2 = 0 then ValueSome() else ValueNone

// ── Decl: multi-statement sequences (virtual semicolons) ────────────────────
module L04_Sequences =
    let sideEffects () =
        printfn "one"
        printfn "two"
        printfn "three"

    let bindThenUse () =
        let x = 1
        let y = 2
        x + y

    let explicitSemi () = stepA (); stepB (); stepC ()

// ── Then: if / elif / else ──────────────────────────────────────────────────
module L05_If =
    let inlineIf a = if a then 1 else 0

    let ownLineIf a =
        if a then
            1
        else
            0

    let chained a b c =
        if a then
            "a"
        elif b then
            "b"
        else
            "c"

    // An else-BODY whose first statement is an `if` (on its OWN line, indented) is a
    // multi-statement body — NOT an `else if`/elif. Here the if-then is followed by a
    // `match`, so the `else` must open a body (the inline `else if` form still flattens).
    let elseBodyStartsWithIf x =
        if x > 0 then
            "pos"
        else
            if x = 0 then ignore ()
            match x with
            | -1 -> "neg-one"
            | _ -> "neg"

    let ifInArg f a b =
        f (
            if a then b
            else 0
        )

// ── Match: arms, guards, nesting ────────────────────────────────────────────
module L06_Match =
    let simple x =
        match x with
        | 0 -> "zero"
        | _ -> "other"

    let guarded n =
        match n with // a trailing comment here must not break the arm list
        | x when x < 0 -> "neg"
        | 0 -> "zero"
        | _ -> "pos"

    // And-pattern (conjunction) — both sub-patterns (often active patterns) match.
    let classify x =
        match x with
        | Even n & GreaterThan 10 -> "big even"
        | _ -> "other"

    // Named-field DU pattern with the fields on their own lines (offside).
    let destructure node =
        match node with
        | Binding(
            headPat = p
            returnInfo = None
            trivia = t) -> p, t
        | _ -> failwith "?"

    // Multi-line RECORD pattern: fields on their own lines (offside, no `;`), and a
    // field value that is itself an or-pattern (`A | B`). Also nested in a `::` cons.
    let recordPat token =
        match token with
        | { Kind = Ident
            Token = t } -> t
        | { Kind = Other | Dot
            Token = t } :: _ -> t
        | _ -> ""

    let nested g a c =
        match g with
        | 1 ->
            match a with
            | 2 ->
                match c with
                | 3 -> "deep"
                | _ -> "mid"
            | _ -> "shallow"
        | _ ->
            if a = 0 then "x"
            else "y"

    // try/with with a multi-statement body (and inside a CE).
    let tryMulti () =
        async {
            try
                do! step1 ()
                do! step2 ()
                return! finish ()
            with ex ->
                return! recover ex
        }

    // `let!` binding a type-annotated FIRST tuple element.
    let bindTyped () =
        task {
            let! (a: int, b: string) = fetch ()
            return a, b
        }

// ── Match: function / lambda ────────────────────────────────────────────────
module L07_Lambda =
    let inc = fun x -> x + 1
    let asFunction =
        function
        | 0 -> "zero"
        | _ -> "other"

    let mapMatch m2 =
        Map.map (fun k v ->
            match v with
            | Some p -> p
            | None -> k) m2

// ── Let: let … in ───────────────────────────────────────────────────────────
module L08_LetIn =
    let oneLine = let x = 10 in x * 2

    let chainedIn =
        let x = 1 in
        let y = 2 in
        x + y

// ── Do: for / while ─────────────────────────────────────────────────────────
module L09_Loops =
    let forIn () =
        for x in [ 1; 2; 3 ] do
            printfn "%d" x

    let forRange () =
        for i in 1..10 do
            printfn "%d" i
            printfn "again %d" i

    let forCountWildcard () =
        for _ = 0 to 9 do  // `_` binder when the index is unused
            printfn "tick"

    let forTuple () =
        for k, v in pairs do
            printfn "%A %A" k v

    // Type-annotated for-binders: bare (`for s: string in`), parenthesised
    // (`for (line: string) in`), and a typed unparenthesised tuple (`for k: T, r in`).
    let forTyped (xs) (rows) =
        for s: string in xs do
            ignore s
        for (line: string) in xs do
            ignore line
        for k: string, r: string in rows do
            ignore (k, r)

    // Union-case / active-pattern application binder, e.g. iterating a dictionary.
    let forKeyValue (dict) =
        for KeyValue(k, v) in dict do
            printfn "%A %A" k v

    // Binder with an `as` alias capturing the whole element.
    let forAs (rows) =
        for (col, _, _) as item in rows do
            ignore item

    let whileLoop () =
        let mutable i = 0
        while i < 3 do
            i <- i + 1

    // `:=` ref-cell assignment (and `do`-statement form).
    let refLoop () =
        let counter = ref 0
        do counter := 0
        while !counter < 3 do
            counter := !counter + 1

// ── Brackets: lists / arrays ────────────────────────────────────────────────
module L10_ListsArrays =
    let inlineList = [ 1; 2; 3 ]
    let inlineArray = [| 1; 2; 3 |]

    let ownLineList =
        [
            1
            2
            3
        ]

    let ownLineArray =
        [|
            10
            20
        |]

    let withSideEffectElem = [ (sideEffect (); value); other ]

    // Dotless indexing (F# 6+): no space before `[` is an index; a space is
    // application to a list literal.
    let first xs = xs[0]
    let slice xs = xs[1..]
    let head2 xs = xs[..2]
    let appliedToList = List.map id [ 1; 2; 3 ]   // space → application, not index

// ── Brackets: records ───────────────────────────────────────────────────────
module L11_Records =
    type Point = { X: int; Y: int }

    // Attribute on a record field (own-line and same-line forms).
    type Dto =
        {
            Name: string
            [<JsonRequired>]
            Age: int
            [<DefaultValue>] mutable Score: int
        }

    let inlineRec = { X = 1; Y = 2 }

    let ownLineRec =
        {
            X = 1
            Y = 2
        }

    let copyRec =
        { ownLineRec with
            X = 42
        }

    let newlineFields =
        { X = 1   // trailing comment must not absorb the next field
          Y = 2 }

    // Type-qualified field name disambiguates which record type (own-line form).
    let qualifiedFields =
        {
            Point.X = 1
            Y = 2
        }

// ── Brackets: computation expressions ───────────────────────────────────────
module L12_ComputationExpr =
    let asyncResult =
        async {
            let! x = async { return 42 }
            do! Async.Sleep 0
            return x
        }

    let asyncIf cond =
        async {
            if cond then
                return "big"
            else
                return! fallback ()
        }

    let asyncUseDo () =
        async {
            if cond then
                use fs = openIt ()

                do! write fs |> Async.AwaitTask
            use stream =
                { new System.IDisposable with
                    member _.Dispose() = ()
                }

            stream.Dispose()
        }

    let seqFor =
        seq {
            for x in [ 1; 2; 3 ] do
                yield x * 2
        }

// ── Decl: types (DU / record / alias) ───────────────────────────────────────
module L13_Types =
    type Shape =
        | Circle of float
        | Rectangle of float * float
        | Point

    // Non-indented DU cases (at the `type` column) followed by a module-level
    // declaration — the type body must close at `open`, not absorb it.
    type Token =
    | Plus
    | Minus of int

    open System

    let tokenZero = Plus

    type Person =
        {
            Name: string
            Age: int
        }

    type MyInt = int
    type Pair = int * string

    // Attribute on a non-first tuple parameter element.
    type Mapper() =
        member _.Map(f, [<InjectAttribute>] comparer: int) = f comparer

    // F# 9 nullable reference types `T | null` (annotation positions; use parens
    // inside a union case).
    let orNull (s: string | null) : string | null = s
    type Holder = { value: string | null }
    // Nullable as the RETURN of a function type (`A -> T | null`), incl. inside a DU
    // case alongside a following case (the `|` after `'a` is the next case, not a nullable).
    let parseOpt (f: string -> int | null) = f
    type Parser = A of (int -> string | null) | B of int
    type Outcome =
        | Ok of int
        | Failed of (string | null)

    // `T | null` as a generic type argument — unambiguous inside `<…>`, both in a
    // type annotation and as an expression-level generic instantiation.
    let mkBag (d: Dictionary<string, ResizeArray<string | null>>) = d
    let empty () = Dictionary<string | null, int>()

    // `T | null` in a comma-separated member / constructor parameter.
    type Tag(text: string | null) =
        static member attr(name: string, value: string | null) = value

    // ML-style prefix type parameters (before the name).
    type 'T container = System.Collections.Generic.List<'T>
    type ('k, 'v) lookup = Map<'k, 'v>

    // Constraint clause OUTSIDE the `<…>` list, and an attribute on a type param.
    type Bag<[<EqualityConditionalOn>] 'T> when 'T: comparison = 'T list

    // Record type with consecutive `mutable` fields (own-line form).
    type Cursor =
        {
            mutable index: int
            mutable active: bool
        }

    // Abstract members with attributes on labelled params (Fable interop).
    type IConsole =
        abstract log: [<ParamArray>] args: obj[] -> unit
        abstract assert': condition: bool * [<ParamArray>] data: obj[] -> unit
        abstract write: source: #IDisposable * ?offset: int -> unit  // flexible-type labelled param

    type Node =
        // Bare (un-parenthesized) curried optional param `?loc`.
        static member leaf ?loc : Node = Node.leaf

    // `new 'T()` — construct a generic type parameter (needs a `new` constraint).
    let inline createDefault<'T when 'T: (new: unit -> 'T)> () = new 'T()

// ── Decl: nested modules ────────────────────────────────────────────────────
module L14_NestedModules =
    module Inner =
        module Deeper =
            let value = 1

    module Sibling =
        let other = 2

    // FSI / script trailing `;` after a declaration.
    module WithSemis =
        open System;
        let value = 1

// ── Decl: classes & members ─────────────────────────────────────────────────
module L15_Classes =
    type Counter() =
        let mutable n = 0
        member _.Bump () = n <- n + 1
        member _.Value = n

    type Animal(name: string) =
        member _.Name = name
        member this.Describe () =
            sprintf "%s" name

    // Attribute on the primary constructor (Fable interop "ParamObject" style).
    type Options
        [<ParamObject>]
        (
            ?width: int,
            ?height: int
        ) =
        member val Width = defaultArg width 0 with get, set

    // Consecutive auto-properties without accessors — each init expression must
    // close before the next `member val` (and an inline `with get, set` still
    // attaches, as on Width above).
    type Bag2(a: int, b: string) =
        member val First = a
        member val Second = b
        // Access modifier on an auto-property.
        member val public Third = 0 with get, set

    // Object expression whose `{` ends the line, with `new` on the next.
    type Factory() =
        member _.Make () = {
            new System.IDisposable with
                member _.Dispose () = ()
        }

// ── Match: type augmentation `with` (the overloaded-`with` case) ────────────
module L16_Augmentation =
    type Reference =
        | Reference of id: int
        | GlobalReference of id: int
    with
        static member toId = function | Reference x | GlobalReference x -> x
        member this.Self = this

// ── Accumulated-state regressions (must stay 0 errors) ──────────────────────
module L17_AccumulatedState =
    // The `dd` repro: class-lets + member + two if-then blocks with do!/use.
    type Writer() =
        let stream = obj ()

        member _.Flush () =
            async {
                if mapsEnabled then
                    let mapPath = "x.map"

                    do!
                        writeLine mapPath
                        |> Async.AwaitTask

                do! flush () |> Async.AwaitTask

                let! written = check ()

                if written && mapsEnabled then
                    use fs = openFile ()

                    do! serialize fs |> Async.AwaitTask

                dispose ()
            }

// ── Types: flexible (subtype) constraints `#T` ──────────────────────────────
module L18_FlexibleTypes =
    // `#T` means "T or any subtype". Valid in parameter and return annotations,
    // as the operand of a function type, and as an upcast target.
    let dispose (x: #IDisposable) = x.Dispose ()

    let firstOf (xs: #seq<int>) = Seq.head xs

    let using (res: #IDisposable) (body: #IDisposable -> unit) = body res

    let asComparable (x: obj) = x :> #IComparable

    // Flexible-type intersection: a subtype of every listed interface (F# 7+).
    let logRead (env: #IReader & #ILogger) = env

    type Sink() =
        member _.Accept(items: #seq<'T>) = Seq.length items

// ── Expr: dynamic-access operator `?` (Fable JS interop) ────────────────────
module L19_DynamicAccess =
    // `obj?member` is the dynamic-lookup operator; chains and is callable.
    let color (el) = el?style?color

    let join (path) = path?join ("a", "b")

    // Member access / method call on the RESULT of a dynamic lookup, without parens
    // (`info?name.AsString()`, `node?pages.Length`) — common in FSharp.Data.
    let dynMember (info) = info?name.AsString()
    let dynIndexed (docs) = docs.[0]?pages.AsInteger()

    // `?(expr)` computed key, and dynamic set with `<-`.
    let lookup (o) (k) = o?(k)

    let setX (o) = o?x <- 1

    // `%`/`%%` splice (anti-quotation) prefix — in real quotations and Fable.
    let spliceInQuote (e) = <@ %e + 1 @>
    let spliceField (this) = { Order.Id = %this.Id }

// ── Expr: element-DSL computation expression (Oxpecker-style) ───────────────
module L20_ElementDSL =
    // The CE builder is an APPLICATION `tag(args)`, not a bare name; the body is
    // child elements. Nests and mixes with ordinary statements/applications.
    let page =
        div() {
            h1() { "Title" }
            p(id) { "paragraph" }
        }

    // A plain application statement right after one must still parse as two calls
    // (the scanner only treats `tag(args) {` — with a following brace — as a DSL).
    let render () =
        div() { "x" }
        ignore ()

    // Builder applied to a STRING name (Fun.Build / Saturn style), not parens.
    let build =
        pipeline "Build" {
            description "Default build pipeline"
            stage "Compile" { run "dotnet build" }
        }

    // Oxpecker.Solid component: the builder argument is an ANONYMOUS RECORD of props,
    // followed by a `{ … }` children body (`Component {| props |} { children }`).
    let component_ =
        navLink {| Class = "nav-link"; Href = "/" |} {
            span(class'="icon") { "icon" }
            " Home"
        }

    // Fluent method chain on the builder before the body — chain links may sit on
    // their own lines (`.hxBoost`/`.hxTarget` are Oxpecker.Htmx modifiers).
    let chained =
        div(class'="card")
            .hxBoost(true)
            .hxTarget("#main") {
            h2() { "Live" }
        }

    // Richer tree: multi-LINE arguments (leading-comma style — args may span
    // lines, the `) {` stays on one line), named args with spaces, a nested
    // `For(each=…)` builder yielding a lambda, deep nesting, an interpolated
    // string child, and `if/else` whose branches are themselves element DSLs.
    let view items handleClick =
        div(class'="container"
            , id="main") {
            h1(style = "color: red") { "Title" }

            ul(class'="list") {
                For(each=items) {
                    yield fun item _ ->
                        li(class'="row") {
                            span(class'="name") { item.Name }
                            button(onClick = handleClick) { "Buy" }
                        }
                }
            }

            if items.Length = 0 then
                p() { "No items" }
            else
                p(class'="count") { $"{items.Length} items" }
        }

// ── Brackets: inline-FIRST list comprehension ───────────────────────────────
// First element on the SAME line as `[` (`[ yield x`), then newline-aligned
// elements — the bracket context opens at the inline element's column so the
// later lines still separate (instead of merging into one application).
module L21b_InlineFirstComprehension =
    let res =
        [ yield constructors
          if not isModule then
              yield! fields
              if Seq.length fields > 0 then
                  yield nl
              yield! funcs ]
        |> Seq.distinct

    // Same in a CE builder (`seq { x`⏎`  y }`), first element inline.
    let items =
        seq { yield head
              yield! tail }

// ── Brackets: `head { … }` — CE vs application(object-expression / record) ───
// A `{` right after a value is a CE body ONLY when its content is a CE body; when
// the content is an object expression (`{ new … }`), a record (`{ field = … }`), or
// a copy-update (`{ base with … }`), `head { … }` is an APPLICATION whose argument is
// that object/record. The scanner classifies the brace content (CE_BRACE_OPEN) so the
// builder stays a plain name and the genuine CE forms below keep working.
module L22_BraceArgDisambiguation =
    // Application whose argument is an OBJECT EXPRESSION (the case that used to cascade
    // errors). Both a dotted head and a bare head.
    let accept (shape: Shape) =
        shape.Accept { new IVisitor with
                         member _.Visit() = 1 }

    let make () =
        build { new IWidget with
                  member _.Render() = "" }

    // Application whose argument is a RECORD / a COPY-UPDATE record.
    let writeRec () = writeJson { Name = "a"; Age = 1 }
    let writeCopy r = writeJson { r with Name = "b" }
    // Application whose argument is an anonymous record / copy-update anon record.
    let writeAnon () = writeJson {| Name = "a"; Age = 1 |}
    let writeAnonCopy r = writeJson {| r with Name = "b" |}

    // Standalone object expression / record / copy-update (no head) — unchanged.
    let disposable = { new System.IDisposable with member _.Dispose() = () }
    let point = { X = 1; Y = 2 }
    let moved p = { p with X = 42 }

    // Genuine computation expressions must STILL parse as CEs (content is a CE body):
    let ceReturn = task { return 1 }
    let ceBind () = task { let! x = fetch () in return x }
    let ceFor xs = seq { for x in xs do yield x * 2 }
    let ceCons head tail = seq { yield head; yield! tail }     // `;`-separated
    let ceConsExpr h t = seq { h :: t }                        // `::` is cons, not a record `:`
    let ceRange = seq { 1..100 }
    let ceApp x = async { return! finish x }

    // Builder applied to a string/expr arg before a record/copy-update argument must
    // NOT be mistaken for the Fun.Build element DSL (the brace content decides).
    let equalRec a = expectEqual "msg" { a with Line = a.Line + 1 }
    let equalRec2 () = expectEqual "msg" { Name = "x" }

// ── Brackets: bare element-DSL as a top-level statement (build.fsx pattern) ──
// A col-0 element-DSL that is NOT the first top-level item (it follows another
// declaration / dedents out of a module). At the top-level line boundary the marker
// must still fire — otherwise `pipeline "x"` reduces to an application and `{ … }` errors.
let private topLevelCfg = 0

pipeline "Deploy" {
    description "top-level element DSL after a declaration"
    runIfOnlySpecified true
}

// A `#time` (or any no-argument) directive must NOT swallow the next line's first
// identifier as its argument — the following bare expression stays its own statement.
#time

seq { 1..100 } |> Seq.map id |> ignore
