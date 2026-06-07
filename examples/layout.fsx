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
    let curriedOwnLine a b =
        a * b

    // Member access directly on a literal.
    let litLen = "abc".Length
    let litChars = "bcd".ToCharArray()
    let charStr = 'x'.ToString()

    // Operator as a first-class value.
    let add = (+)
    let combine = List.reduce (+)

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
        match n with
        | x when x < 0 -> "neg"
        | 0 -> "zero"
        | _ -> "pos"

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

    let forTuple () =
        for k, v in pairs do
            printfn "%A %A" k v

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

// ── Brackets: records ───────────────────────────────────────────────────────
module L11_Records =
    type Point = { X: int; Y: int }

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

    type Person =
        {
            Name: string
            Age: int
        }

    type MyInt = int
    type Pair = int * string

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

// ── Decl: nested modules ────────────────────────────────────────────────────
module L14_NestedModules =
    module Inner =
        module Deeper =
            let value = 1

    module Sibling =
        let other = 2

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

    type Sink() =
        member _.Accept(items: #seq<'T>) = Seq.length items

// ── Expr: dynamic-access operator `?` (Fable JS interop) ────────────────────
module L19_DynamicAccess =
    // `obj?member` is the dynamic-lookup operator; chains and is callable.
    let color (el) = el?style?color

    let join (path) = path?join ("a", "b")

    // `?(expr)` computed key, and dynamic set with `<-`.
    let lookup (o) (k) = o?(k)

    let setX (o) = o?x <- 1
