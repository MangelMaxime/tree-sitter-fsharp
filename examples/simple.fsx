#!/usr/bin/env -S dotnet fsi

// ── F# Interactive directives (typically used in .fsx scripts) ───────────────

#load "helper.fsx"
#r "nuget: Newtonsoft.Json"
#I "/path/to/lib"
#nowarn "25"
#nowarn 42
#time
#time on
#time off
// #help
// #quit

// File-level module declaration (no = sign, rest of file is its body)
// module ScriptHelpers

open System
open System.Text.RegularExpressions
open type System.Math

// This is a comment
(* This is a block comment *)
(** This is a doc comment that should inject markdown

# Heading
Some *italic* and **bold** text.

```fsharp
let x = 42
```

- Item 1
- Item 2

[link](https://example.com)
*)

(** This a **strong** comment *)

let valule_byte = 1uy
let valule_sbyte = 1y
let valule_int16 = 1s
let valule_uint16 = 1us
let valule_int = 1
let valule_uint = 1u
let valule_int64 = 1L
let valule_uint64 = 1UL
let value_hex = 0xFF
let value_binary = 0b1010
let value_octal = 0o755
let value_bigint = 123456789012345678901234567890I
let scientific = 1.23e-4
let valule_nativeint = nativeint 1
let valule_unativeint = unativeint 1
let valule_decimal = 1.0m
let valule_decimal_1 = 1m
let valule_float = 3.14
let valule_float32 = 1.0f
let valule_float_no_frac = 180.e5
let valule_float_no_frac_f = 180.e5f
let valule_float_hex_lf = 0x00000000lf
let valule_float_hex_LF = 0x0000000000000000LF
let valule_char = 'c'
let valule_char_escape = '\n'
let valule_char_decimal = '\097'
let valule_char_hex = '\x41'
let valule_char_unicode = 'A'
let valule_char_byte = 'F'B
let valule_string = "str"
let valule_string_escape = "hello\nworld"
let valule_string_unicode = "élève"
let valule_byte_string = "hello"B
let valule_verbatim = @"C:\Users\foo"
let valule_verbatim_quote = @"say ""hello"""
let valule_verbatim_bytes = @"raw"B
let valule_triple = """triple "quoted" string"""

let valule_triple_multiline =
    """
first line
second "line"
"""

let valule_unit = ()

let pi = PI

printfn "Hello"

let answer = 42

/// <summary>Add 2 number</summary>
/// <param name="a"></param>
/// <param name="b"></param>
/// <returns>Result of the addition of the **2** numbers</returns>
let add a b = a + b

let sub (a: int) b = a - b

// Type annotations
let typed_val: int = 42
let typed_fn: int -> string = failwith "todo"
let typed_fn2: int -> int -> int = failwith "todo"
let typed_tuple: int * string = failwith "todo"
let typed_generic: list<int> = []
let typed_postfix: int list = []
let typed_array: int[] = [||]
let typed_param<'a> : 'a -> 'a = failwith "todo"
let typed_complex<'a> : 'a list -> int = failwith "todo"
let typed_compound: int * string -> bool = failwith "todo"

let rec fib4 (n: int) : int =
    match n with
    | 1
    | 2 -> n
    | n -> fib4 (n - 1) + fib4 (n - 2)

let max a b =
    if a > b then
        a
    else
        b

let condition a b c =
    if a then
        "a"
    else if b then
        "b"
    elif c then
        "c"
    else
        "nothing"

let app_if_arg f a b =
    f (
        if a then
            b
        else
            0
    )

Environment.CurrentDirectory |> printfn "%A"

let r = Regex.Escape "dw"

let inc = fun x -> x + 1
let add_lambda = fun x y -> x + y
let typed_lambda = fun (x: int) -> x * 2

// Arithmetic
let arith_add = 1 + 2
let arith_sub = 10 - 3
let arith_mul = 4 * 5
let arith_div = 10 / 2
let arith_mod = 10 % 3
let arith_prec = 1 + 2 * 3 // 1 + (2 * 3) = 7
let arith_paren = (1 + 2) * 3 // (1 + 2) * 3 = 9

// Boolean
let bool_and = true && false
let bool_or = true || false
let bool_prec a b c d = a > b && c > d // (a > b) && (c > d)
let bool_short a b c = a || b && c // a || (b && c)

// Pipe
let pipe_right =
    [
        1
        2
        3
    ]
    |> List.length

let pipe_chain x f g = x |> f |> g
let pipe_left f x = f <| x

// Unary
let unary_not = not true
let unary_not2 = not false
let bitwise_not = ~~~42

// Cons
let cons_one x xs = x :: xs
let cons_chain rest = 1 :: 2 :: 3 :: rest

let (>>=) a b = ignore
let (<|>) a b = ignore

// Custom symbolic operators
let bind_result result next = result >>= next
let choice_op parser1 parser2 = parser1 <|> parser2
let append_list xs ys = xs @ ys

// let inline / mutable
let inline square x = x * x
let inline add_inline a b = a + b
let mutable counter = 0
let mutable message = "hello"

// let rec is already covered above (fib4)

// let ... in ...
let let_in_result = let x = 10 in x * 2

let let_in_nested =
    let x = 1 in
    let y = 2 in
    x + y

let let_in_fn = let double x = x * 2 in double 21

// Lists
let empty_list = []
let singleton_list = [ 42 ]

let int_list =
    [
        1
        2
        3
    ]

let str_list =
    [
        "a"
        "b"
        "c"
    ]

// Arrays
let empty_array = [||]

let int_array =
    [|
        10
        20
        30
    |]

// Index expressions
let idx_single = int_array.[0]
let idx_range = int_array.[0..1]
let idx_open_end = int_array.[1..]
let idx_open_start = int_array.[..1]
let idx_dict = Map.ofList [ ("key", 42) ]
let idx_str_key = idx_dict.["key"]
let idx_matrix_src = Array2D.init 2 2 (fun i j -> i + j)
let idx_matrix = idx_matrix_src.[0, 1]

// Chained member access
let dot_after_index = int_array.[0..1].Length
let dot_after_parens = ("hello").Length
let dot_chained = int_array.[0..1].Length.ToString()

// Tuples
let pair = 1, 2
let triple = "hello", 42, true
let paren_pair = (1, 2)

// When guards
let classify n =
    match n with
    | x when x < 0 -> "negative"
    | 0 -> "zero"
    | x when x > 100 -> "large"
    | _ -> "positive"

let classify_exact n =
    match n with
    | -2 -> "minus two"
    | -1 -> "minus one"
    | 0 -> "zero"
    | 1 -> "one"
    | _ -> "other"

// List, cons and as patterns
let describe_list xs =
    match xs with
    | [] -> "empty"
    | [ x ] -> "one element"
    | [ x; y ] as pair -> "two elements"
    | _ -> "many"

let rec sum_list acc xs =
    match xs with
    | [] -> acc
    | x :: rest -> sum_list (acc + x) rest

let chain_cons xs =
    match xs with
    | a :: b :: rest -> a + b
    | x :: [] -> x
    | [] -> 0

// Discriminated union type definitions
type Shape =
    | Circle of float
    | Rectangle of float * float
    | Point

type MyResult<'ok, 'err> =
    | Ok of 'ok
    | Error of 'err

type Tree<'a> =
    | Leaf
    | Node of 'a * Tree<'a> * Tree<'a>

type Color =
    | Red
    | Green
    | Blue

// Enum type definitions
type FilePermission =
    | None = 0
    | Read = 1
    | Write = 2
    | ReadWrite = 3

type Nibble =
    | Low = 0x0F
    | High = 0xF0

// DU match patterns
let area shape =
    match shape with
    | Circle r -> r * r * 3.14159
    | Rectangle (w, h) -> w * h
    | Point -> 0.0

let unwrap opt =
    match opt with
    | Some x -> x
    | None -> 0

// Mutual type recursion
type MutualEven =
    | MZero
    | MSuccEven of MutualOdd

and MutualOdd =
    | MSuccOdd of MutualEven

type JsonValue =
    | JNull
    | JBool of bool
    | JNumber of float
    | JString of string
    | JArray of JsonArray // Check that the line below is highlighted correctly even with this comment
    | JObject of JsonObject

and JsonArray = JsonValue list

and JsonObject = (string * JsonValue) list

// Named union fields
type Contact =
    | Email of address: string
    | Phone of number: int * extension: int option
    | Address of street: string * city: string * zip: string

type BinTree<'a> =
    | Leaf
    | Node of value: 'a * left: BinTree<'a> * right: BinTree<'a>

// Record type definitions
type Point =
    {
        X: int
        Y: int
    }

type Person =
    {
        Name: string
        Age: int
    }

type MutableCounter =
    {
        mutable Count: int
    }

type Rect =
    {
        TopLeft: Point
        BottomRight: Point
    }

type Wrapper<'A, 'B> =
    {
        Z : unit -> unit
        A : 'A
        B : 'A
    }

// Attributes
[<Obsolete("use newAdd instead")>]
let oldAdd x y = x + y

[<AutoOpen>]
[<RequireQualifiedAccess>]
type AttributedUnion =
    | [<DefaultValue>] X
    | Y

type [<RequireQualifiedAccess;NoComparison>] FooAttributedUnion =
    | Value of int

[<Literal>]
let MaxCount = 100

// Multiple attributes in a single bracket (semicolon-separated)
[<AutoOpen; RequireQualifiedAccess>]
module MultiAttr =
    let value = 1

// Type aliases
type MyInt = int
type StringList = string list
type Point2D = float * float
type StringMap<'v> = Map<string, 'v>
type Predicate<'a> = 'a -> bool

type Foo() =
      member this.Bar(
          [<System.Runtime.InteropServices.Optional;
            System.Runtime.InteropServices.DefaultParameterValue(42)>] x: int
      ) = x

// Caller info attributes
type Logger1() =
  member this.Log(
      msg: string,
      [<System.Runtime.CompilerServices.CallerMemberName>] ?memberName: string,
      [<System.Runtime.CompilerServices.CallerFilePath>] ?filePath: string
  ) = ()

// ParamArray (params)
let format ([<System.ParamArray>] args: obj[]) = ()

// Record expressions
let record_creation =
    {
        X = 1
        Y = 2
    }

let record_trailing =
    {
        X = 10
        Y = 20
    }

let record_copy =
    { record_creation with
        X = 42
    }

let record_copy_multi =
    { record_creation with
        X = 0
        Y = 0
    }

let record_named =
    {
        Name = "Alice"
        Age = 30
    }

let record_copy_person =
    { record_named with
        Age = 31
    }

let record_expr_val =
    {
        X = abs 3
        Y = max 5 10
    }

let record_nested =
    {
        TopLeft =
            {
                X = 0
                Y = 0
            }
        BottomRight =
            {
                X = 100
                Y = 100
            }
    }

// Edge case: a trailing line-comment on a record field's value must not cause
// the value expression to greedily absorb the next field's name. Both `Y` and
// `Z` here should highlight as record fields (variable.other.member), not as
// part of the previous value's expression.
let record_with_comments =
    {
        X = 1 // trailing comment on the value
        Y = 2
    }

module Lib =
    module Math =
        module Integer =
            let x = 1

// Module abbreviations (aliases)
module L = Lib
module LMI = Lib.Math.Integer

// Nested modules (valid in .fsx scripts)
module MathUtils =
    let square x = x * x
    let cube x = x * x * x

module private StringUtils =
    let upper (s: string) = s.ToUpper()
    let lower (s: string) = s.ToLower()

module rec Recursive =
    let even n = if n = 0 then true else odd (n - 1)
    let odd n = if n = 0 then false else even (n - 1)

// Modules with attributes and access modifiers (from module.fs)
[<AutoOpen>]
module Utils =
    let addUtil a b = a + b
    let subUtil a b = a - b

module private Internals =
    let secret = 42

module rec MutuallyRecursive =
    let isEven n = if n = 0 then true else isOdd (n - 1)
    let isOdd n = if n = 0 then false else isEven (n - 1)

// OOP: interfaces, classes, inheritance
type IAnimal =
    abstract member Name: string
    abstract member Sound: unit -> string

type IShape =
    abstract member Area: float
    abstract member Perimeter: float

// Abstract property accessors and generic abstract members.
type IFooBar =
    // Read-write abstract property
    abstract member ReadWrite : int with get, set

    // Read-only abstract property
    abstract member ReadOnly : string with get

    // Write-only abstract property
    abstract member WriteOnly : float with set

    // Generic abstract method
    abstract member Map<'T> : 'T -> 'T

    // Generic abstract method with constraint
    abstract member Cast<'T, 'U when 'T : comparison> : 'T -> 'U

    // `abstract` without the `member` keyword (legal in F#)
    abstract Short : int with get, set

type Dog(name: string, breed: string) =
    member this.Name = name
    member this.Breed = breed
    member this.Bark() = "Woof!"
    member this.Describe() = sprintf "%s is a %s" name breed
    override this.ToString() = sprintf "Dog(%s)" name
    interface IAnimal with
        member this.Name = name
        member this.Sound() = "Woof!"

type Logger(tag: string) =
    do printfn "Logger[%s] initialized" tag
    member this.Tag = tag
    member this.Log msg = printfn "[%s] %s" tag msg

type Cat(name: string) =
    inherit Dog(name, "Domestic Cat")
    member this.Purr() = "Purrrr..."
    interface IAnimal with
        member this.Name = name
        member this.Sound() = "Meow!"

type Wrapper2<'T>(value: 'T) =
    member this.Value = value
    member this.Map (f: 'T -> 'T) = Wrapper2(f value)

// ── Generic methods ──────────────────────────────────────────────────────────

// Member with its OWN type parameters (separate from the class's). Common in
// real F# code — was previously parsing as ERROR.
type GenericMethods() =
    // Single type parameter
    member this.Identity<'T> (x: 'T) : 'T = x

    // Multiple type parameters + return type
    member this.Cast<'T, 'U> (x: 'T) : 'U = failwith "todo"

    // Generic static member
    static member Single<'T> (x: 'T) = [ x ]

    // Static + inline + constraint
    static member inline Add<'T when 'T : (static member (+): 'T * 'T -> 'T)> (a: 'T) (b: 'T) : 'T =
        a + b

    // Generic method on a generic class (its own 'U is independent of class's 'T)
    // (Already supported via Wrapper2, but worth a dedicated example.)
    member this.AsList<'U> (x: 'U) = [ x ]

// Generic method on a generic type
type Container<'T>(value: 'T) =
    member this.Map<'U> (f: 'T -> 'U) : Container<'U> = Container(f value)

type MathHelperClass =
    static member Square x = x * x
    static member Cube x = x * x * x
    static member Pi = 3.14159265358979
    static member CircleArea (radius: float) =
        MathHelperClass.Pi * radius * radius

type Holder() =
    [<DefaultValue>]
    val mutable data: int
    member this.Data = this.data

// ── Property get/set accessors ────────────────────────────────────────────────

type Counter() =
    let mutable _count = 0
    member this.Count with get() = _count
    member this.Value
        with get() = _count
        and set(v) = _count <- v
    member this.Raw with set v = _count <- v
    static member Zero with get() = 0

type AutoProps() =
    member val Name = "" with get, set
    member val Age = 0 with get, set
    member val ReadOnly = 42 with get
    static member val DefaultName = "default" with get, set

// _ as self identifier (no self reference needed)
type Singleton() =
    static member Instance = Singleton()
    override _.ToString() = "Singleton"

// Optional primary constructor parameter
type Connection(?host: string, ?port: int) =
    member _.Host = defaultArg host "localhost"
    member _.Port = defaultArg port 5432

// ── Object expressions ───────────────────────────────────────────────────────

let obj_basic =
    { new IAnimal with
        member this.Name = "anonymous"
        member this.Sound() = "..."
    }

let obj_base_ctor =
    { new System.Exception("custom") with
        override this.Message = "overridden"
    }

let obj_multi_interface =
    { new IAnimal with
        member this.Name = "runner"
        member this.Sound() = "go"
      interface IShape with
        member this.Area = 0.0
        member this.Perimeter = 0.0
    }

// ── New object ───────────────────────────────────────────────────────────────

let ex = new System.Exception("test")
let ex2 = new System.ArgumentException("bad arg", "paramName")
let dict = new System.Collections.Generic.Dictionary<string, int>()

// ── Exceptions ───────────────────────────────────────────────────────────────

exception MyError
exception MyErrorWithMessage of string
exception ParseError of string * int

let safe_divide a b =
    try
        a / b
    with
    | :? System.DivideByZeroException -> 0
    | _ -> -1

let parse_int (s: string) =
    try
        int s
    with
    | :? System.FormatException as ex -> 0
    | :? System.OverflowException -> -1

let with_cleanup () =
    try
        printfn "work"
    finally
        printfn "cleanup"

let raise_example n =
    if n < 0 then
        raise (MyErrorWithMessage "negative")
    n

// ── Type casts ────────────────────────────────────────────────────────────────

type IAnimalBase =
    abstract member Speak: unit -> string

let dog_as_base (d: Dog) : IAnimal = d :> IAnimal
let base_as_dog (a: IAnimal) : Dog = a :?> Dog

let is_string (obj: obj) = obj :? string

let upcast_example (d: Dog) : IAnimal = upcast d
let downcast_example (a: IAnimal) : Dog = downcast a

let type_test_match (obj: obj) =
    match obj with
    | :? string as s -> s
    | :? int as n -> string n
    | _ -> "unknown"

// ── Mutation (<-) ─────────────────────────────────────────────────────────────

let mutable_counter =
    let mutable n = 0 in
    n <- n + 1

let mutable_swap =
    let mutable a = 1 in
    let mutable b = 2 in
    let tmp = a in
    a <- b

// ── For / While loops ────────────────────────────────────────────────────────

let print_items () =
    for x in [ 1; 2; 3 ] do
        printfn "%d" x

let print_range () =
    for i in 1..10 do
        printfn "%d" i

let while_demo =
    let mutable i = 0 in
    while i < 3 do
        i <- i + 1

// Multi-statement body — parses as `sequence_expression` (each statement is
// its own child). Try expand-selection from `"done"`: it should walk to the
// surrounding statement, then to the while body, then to `while_expression`.
let while_multi () =
    let mutable n = 0
    while n < 3 do
        printfn "iter %d" n
        printfn "done"
        n <- n + 1

// Multi-statement body — parses as `sequence_expression` (each statement is
// its own child), symmetric with `while_multi` above. The scanner emits
// `_for_body_open` only when the body's first token isn't a query-CE operator
// or a CE result/bang keyword, so this real loop sequences while
// `query { for x in xs do where … }` keeps the for body empty (see grammar.js).
let for_multi () =
    for i in [ 1; 2; 3 ] do
        printfn "item: %d" i
        printfn "double: %d" (i * 2)

// ── Computation expressions ───────────────────────────────────────────────────

// async — F# built-in async workflow
let async_result =
    async {
        let! x = async { return 42 }
        do! Async.Sleep 0
        return x
    }

let async_if =
    async {
        let! value = async { return 10 }
        if value > 5 then
            return "big"
        else
            return "small"
    }

let async_match =
    async {
        let! value = async { return 42 }
        match value with
        | 0 -> return "zero"
        | n -> return "other"
    }

// seq — lazy sequences
let seq_range =
    seq {
        yield 1
        yield 2
        yield 3
    }

let seq_for =
    seq {
        for x in [ 1; 2; 3 ] do
            yield x * 2
    }

let seq_yield_bang =
    seq {
        yield! [ 1; 2; 3 ]
        yield 4
    }

// task — .NET Task-based async (same keywords as async)
let task_result =
    task {
        let! data = task { return 42 }
        return data * 2
    }

// promise — custom CE builder (same keyword set)
// let promise_result =
//     promise {
//         let! x = fetchAsync ()
//         return x
//     }

// use in expression body (auto-dispose)
let readFile (path: string) =
    use reader = new System.IO.StreamReader(path)   // reader.Dispose() called automatically
    reader.ReadToEnd()

let async_use =
    async {
        use resource = new System.IO.MemoryStream()
        return resource.Length
    }

// match! inside CE
let async_match_bang =
    async {
        match! async { return 42 } with
        | 42 -> return "answer"
        | _ -> return "other"
    }

// ── Query expressions ────────────────────────────────────────────────────────

// Simple where + select
let q_where_select =
    query {
        for x in [ 1..10 ] do
        where (x > 5)
        select x
    }

// Chained custom operators: where → sortBy → take → select
let q_chain =
    query {
        for x in [ 1..100 ] do
        where (x % 2 = 0)
        sortBy x
        take 5
        select (x * x)
    }

// sortByDescending + thenBy
let q_sort_multi =
    query {
        for p in [ "alice", 30; "bob", 25; "carol", 30 ] do
        sortByDescending (snd p)
        thenBy (fst p)
        select p
    }

// groupBy … into
let q_group =
    query {
        for x in [ 1..10 ] do
        groupBy (x % 3) into g
        select (g.Key, g.ToString())
    }

// Aggregating terminals: count / head / last / exists / contains
let q_count =
    query {
        for x in [ 1..100 ] do
        where (x % 5 = 0)
        count
    }

let q_head =
    query {
        for x in [ 5; 3; 8; 1 ] do
        sortByDescending x
        head
    }

let q_exists =
    query {
        for x in [ 1..10 ] do
        exists (x = 7)
    }

// distinct / skip / takeWhile / skipWhile
let q_distinct =
    query {
        for x in [ 1; 1; 2; 3; 3; 4 ] do
        distinct
    }

let q_window =
    query {
        for x in [ 1..20 ] do
        skip 5
        takeWhile (x < 15)
        select x
    }

// minBy / maxBy / sumBy / averageBy
let q_stats =
    query {
        for x in [ 1..10 ] do
        sumBy (x * x)
    }

// Source data for the join examples below
type Customer = { Id: int; Name: string }
type Order    = { CustomerId: int; Total: decimal }

let customers : Customer list =
    [
        { Id = 1; Name = "Alice" }
        { Id = 2; Name = "Bob" }
    ]

let orders : Order list =
    [
        { CustomerId = 1; Total = 42.0m }
        { CustomerId = 1; Total = 10.0m }
        { CustomerId = 2; Total =  5.0m }
    ]

// join … in … on (…)
let q_join =
    query {
        for c in customers do
        join o in orders on (c.Id = o.CustomerId)
        select (c.Name, o.Total)
    }

// leftOuterJoin … into
let q_left_join =
    query {
        for c in customers do
        leftOuterJoin o in orders on (c.Id = o.CustomerId) into orderGroup
        select (c.Name, orderGroup)
    }

// The query custom-operator names remain ordinary identifiers outside CEs.
// These all parse as plain `List.<member>` applications, not query operators.
let q_id_select x = x + 1
let q_id_where x = x > 0
let q_id_distinct_call = List.distinct [ 1; 1; 2 ]
let q_id_take_call = List.take 3 [ 1..10 ]
let q_id_where_call = List.where (fun x -> x > 0) [ -1; 0; 1; 2 ]
let q_id_count_call = List.length [ 1; 2; 3 ]

let lazy_val = lazy (1 + 2)

let safe_assert x = assert (x > 0)

let int_to_string = function
    | 0 -> "zero"
    | 1 -> "one"
    | _ -> "other"

for i = 1 to 5 do
    printfn "%d" i

for i = 5 downto 1 do
    printfn "%d" i

let block_result = begin 1 + 2 end

let rec is_even n = n = 0
and is_odd n = n <> 0

// 'base' keyword — calling a base-class member from an override
type NamedThing(name: string) =
    override _.ToString() = name

type SpecialThing(name: string) =
    inherit NamedThing(name)
    override this.ToString() = "Special: " + base.ToString()

// ── `as this` on primary constructor ────────────────────────────────────────

// Names the constructed instance so members can refer back to it (e.g. when
// the type captures itself in an event handler or a back-reference).
type Watcher(label: string) as this =
    member _.Label = label
    member _.Describe () = sprintf "Watcher %s sees %A" label this

// Identifier is conventional `this` but any name works.
type Counter4(initial: int) as self =
    let mutable n = initial
    member _.Bump () = n <- n + 1
    member _.Snapshot () = (n, self)

// ── Active patterns ───────────────────────────────────────────────────────────

// Complete multi-case
let (|Even|Odd|) n =
    if n % 2 = 0 then Even else Odd

// Single-case
let (|Double|) n = n * 2

// Partial (returns option)
let (|ParseInt|_|) (s: string) =
    match System.Int32.TryParse(s) with
    | true, n -> Some n
    | _ -> None

// Parameterized partial
let (|DivisibleBy|_|) divisor n =
    if n % divisor = 0 then Some () else None

// Usage in match
let parity n =
    match n with
    | Even -> "even"
    | Odd -> "odd"

let try_parse s =
    match s with
    | ParseInt n -> n
    | _ -> 0

let check_div n =
    match n with
    | DivisibleBy 3 -> "fizz"
    | DivisibleBy 5 -> "buzz"
    | _ -> string n

// ── Named DU field patterns ───────────────────────────────────────────────────

let contact_label c =
    match c with
    | Email(address = addr) -> addr
    | Phone(number = n; extension = ext) -> sprintf "%d x%A" n ext
    | Address(street = s; city = city; zip = z) -> sprintf "%s, %s %s" s city z

// ── Record patterns ───────────────────────────────────────────────────────────

let get_x p =
    match p with
    | { X = x } -> x

let is_origin p =
    match p with
    | { X = 0; Y = 0 } -> true
    | _ -> false

let describe_person person =
    match person with
    | { Name = "Alice"; Age = age } -> sprintf "Alice aged %d" age
    | { Name = name; Age = a } when a < 18 -> sprintf "%s is a minor" name
    | { Name = name } -> name

let nested_record_match r =
    match r with
    | { TopLeft = { X = 0; Y = 0 } } -> "origin rect"
    | _ -> "other"

// ── Preprocessor directives ───────────────────────────────────────────────────

#nowarn "25"
#nowarn 15

#if DEBUG
let debug_value = true
#elif TRACE
let debug_value = false
#else
let debug_value = false
#endif

#if TRACE
let complex_condition = true
#endif

// #load "helper.fsx"
#r "nuget: Newtonsoft.Json"

// ── Backtick-quoted identifiers ───────────────────────────────────────────────

type Box() =
    static member ``box--indented`` = failwith "todo"
    member _.``to string`` () = "box"

let ``kebab-case-value`` = 42
let ``value with spaces`` = "hello"

let b =
    if true then
        ignore Box.``box--indented``
    else
        ()

// ── Unicode identifiers ──────────────────────────────────────────────────────

// F# allows Unicode letters and digits in identifiers.
let π = 3.1415
let accentué = "accentué"
let café = "espresso"
let 数学 = 42
let Σ x y = x + y

// Unicode also legal inside member/type names
type Café() =
    member _.Boisson = "noisette"

// ── Anonymous record types ────────────────────────────────────────────────────

type NamedPoint =
    {|
        X: float
        Y: float
    |}

type PersonInfo =
    {|
        Name: string
        Age: int
        Email: string option
    |}

let origin
    : {|
          X: float
          Y: float
      |} =
    {|
        X = 0.0
        Y = 0.0
    |}

let alice =
    {|
        Name = "Alice"
        Age = 30
    |}

let bob =
    {| alice with
        Name = "Bob"
        Age = 25
    |}

let greetAnon
    (p:
        {|
            Name: string
        |})
    =
    printfn "Hello, %s!" p.Name

greetAnon
    {|
        Name = "Charlie"
    |}

// ── Units of measure ─────────────────────────────────────────────────────────

// Measure type declarations (attribute inline and on separate line)
[<Measure>] type cm
[<Measure>] type kg
[<Measure>] type s
[<Measure>] type steps

[<Measure>]
type m

// Measure type aliases
[<Measure>] type ml = cm^3
[<Measure>] type N = kg*m/s^2
[<Measure>] type Pa = N/m^2

// Measure-typed literals (no space before <)
let length    = 3.0<cm>
let mass      = 75.0<kg>
let speed     = 55.0<m/s>
let accel     = 9.8<m/s^2>
let stepCount = 10000u<steps>

// Measure types in type annotations
let distance : float<m> = 100.0<m>

// Measure types in generic position
type Velocity = float<m/s>
type Force     = float<kg*m/s^2>

// Generic function over any unit
let genericSum (x : float<'u>) (y : float<'u>) = x + y

// Comparison still works (space before <)
let isBig = length > 2.0<cm>

// ── String interpolation ──────────────────────────────────────────────────────

// Basic interpolation
let greeting name = $"Hello, {name}!"

// Expression in hole
let info = $"Sum: {1 + 2}"

// Format specifier
let price = 9.99
let formatted = $"Price: {price:F2}"

// Multiple holes
let firstName = "John"
let lastName = "Doe"
let full = $"Name: {firstName} {lastName}"

// Escaped braces (literal { and })
let escaped = $"Braces: {{not a hole}}"

// Verbatim interpolated
let path = @"C:\tmp"
let msg = $@"File at: {path}"

// Triple-quoted interpolated
let multi =
    $"""
    Hello {firstName}!
    Your total is {price:F2}.
    """

// Nested expressions
let cond = $"""Status: {if price > 5.0 then "expensive" else "cheap"}"""

// Printf-style format specifiers (before the {)
let piStr    = $"%0.3f{System.Math.PI}"   // "3.142"
let hexStr   = $"0x%08x{43962}"           // "0x0000abba"
let dataStr  = $"The data is %A{[0..4]}"  // diagnostic %A

// Literal % (not a format spec — no { immediately after)
let pct = $"Progress: 100%% complete"

// .NET-style with double-backtick escaped separator
let dateStr  = $"{System.DateTime.UtcNow:``yyyy-MM-dd``}"

// ── nameof ────────────────────────────────────────────────────────────────────

// Simple identifier
let xxx = 42
let xName = nameof xxx

// Qualified name — returns the last segment
let mathName = nameof System.Math

// Useful for argument validation
let greet (name: string) =
    if name = null then
        invalidArg (nameof name) "name must not be null"
    $"Hello, {name}!"

// ── Generic type constraints ──────────────────────────────────────────────────

// Subtype constraint: 'T must implement IComparable
type Wrapper4<'T when 'T :> System.IComparable> = { value: 'T }

// Comparison constraint on a function
let sort<'T when 'T : comparison> (xs: 'T list) = List.sort xs

// Equality constraint
let distinct<'T when 'T : equality> (xs: 'T list) = List.distinct xs

// Null constraint
type Nullable<'T when 'T : null> = { inner: 'T }

// Not struct (reference type only)
type RefOnly<'T when 'T : not struct> = { inner: 'T }

// Unmanaged constraint (for low-level code)
let inline zeroBits<'T when 'T : unmanaged> (x: 'T) = x

// Enum constraint
type EnumWrapper<'T when 'T : enum<int>> = { tag: 'T }

// Default constructor constraint
type Factory<'T when 'T : (new : unit -> 'T)> = { create: unit -> 'T }

// Multiple constraints with and
type Container<'T, 'U when 'T : comparison and 'U : equality> =
    { key: 'T; value: 'U }

// ── Type augmentations (members declared with the type) ─────────────────────

// `with member …` appended to a type definition — different from a type
// extension (`type Foo with …` with no `=`).

// Record with augmentation, `with` on same line as body
type AugRecord = { X: int; Y: int } with
    member this.Sum = this.X + this.Y

// DU with augmentation, `with` at body column
type Direction2 =
    | North
    | South
    | West
    | East
    with
        member this.Opposite =
            match this with
            | North -> South
            | South -> North
            | East -> West
            | West -> East

// DU with augmentation, `with` at outer column
type Color2 =
    | Red
    | Green
    | Blue
with
    member this.IsPrimary = true

// Class with primary constructor + augmentation
type Box2(value: int) =
    member _.Value = value
with
    member this.IsZero = this.Value = 0

// ── Type extensions ───────────────────────────────────────────────────────────

// Intrinsic extension — adds members to a type defined in the same module
type Counter with
    member this.IsZero = counter = 0

// Generic type extension with a constraint
type Wrapper3<'T when 'T : equality>(value: 'T) =
    class end

type Wrapper3<'T when 'T : equality> with
    member this.Contains (x: 'T) (xs: 'T list) = List.contains x xs

// Extension with static and instance members
type Color with
    static member Random () = Red
    member this.IsWarm =
        match this with
        | Red -> true
        | _ -> false

type Direction =
    | North
    | South
    | West
    | East

// Extension with multiple members
type Direction with
    member this.Opposite =
        match this with
        | North -> South
        | South -> North
        | East -> West
        | West -> East

    member this.IsVertical =
        match this with
        | North | South -> true
        | _ -> false

type System.String with

    static member Additional () = failwith ""

// ── Pattern destructuring in let bindings ────────────────────────────────────

// Tuple destructure
let (a, bb) = (1, 2)
let (xx, y, z) = (1, 2, 3)

// Unparenthesized tuple destructure
let u1, u2 = 1, 2
let u3, u4, u5 = 1, 2, 3

// Record destructure
let { X = px; Y = py } = { X = 10; Y = 20 }

// List destructure
let [first; second] = [1; 2]

// Array destructure
let [| p; q |] = [| 3; 4 |]

// Wildcard
let _ = ignore "unused"

// Nested tuple
let ((a1, a2), b1) = ((1, 2), 3)

// Constructor pattern (partial match)
let (Some value) = Some 42

// In let ... in expressions
let result =
    let (m, n) = (10, 20) in
    m + n

// ── Pattern destructuring in parameters ──────────────────────────────────────

// Tuple params
let swap (a, b) = (b, a)
let fst3 (x, _, _) = x
let add_pair (a, b) = a + b

// Wildcard param
let constant _ = 42
let ignore2 _ _ = ()

// Record param
let getX { X = x } = x
let getXY { X = x; Y = y } = (x, y)

// Nested tuple param
let fst_of_pair ((a, _), _) = a

// Lambda with tuple param
let swap_lambda = fun (a, b) -> (b, a)
let sum_triple = fun (a, b, c) -> a + b + c

// Constructor pattern param
let unwrap_some (Some x) = x

// Mixed: some normal, some destructured
let combine a (x, y) b = a + x + y + b

// Record param in member
type PointHelper() =
    static member Magnitude { X = x; Y = y } = sqrt (float (x*x + y*y))

// ── Or-patterns ──────────────────────────────────────────────────────────────

// Top-level or in match arm
let classify_pattern n =
    match n with
    | 1 | 2 | 3 -> "small"
    | 4 | 5 -> "medium"
    | _ -> "large"

// Nested or-pattern (inside constructor)
let unwrap_result r =
    match r with
    | Ok (0 | 1) -> "zero or one"
    | Ok n -> string n
    | Error _ -> "error"

// Or with cons
let starts_with_small xs =
    match xs with
    | 1 :: _ | 2 :: _ -> true
    | _ -> false

// Or with as — `as` wraps the whole or-pattern
let first_small n =
    match n with
    | 1 | 2 as x -> x
    | _ -> 0

// Or inside list pattern
let head_small xs =
    match xs with
    | (1 | 2 | 3) :: _ -> true
    | _ -> false

// Or in function expression
let parity_pattern = function
    | 0 | 2 | 4 | 6 | 8 -> "even digit"
    | 1 | 3 | 5 | 7 | 9 -> "odd digit"
    | _ -> "not a digit"

// Or with type-test pattern
let describe (obj: obj) =
    match obj with
    | :? int | :? float -> "numeric"
    | :? string -> "text"
    | _ -> "other"

// ── Typed expressions (expr : type) ─────────────────────────────────────────

let te_int    = (42 : int)
let te_list   = ([] : int list)
let te_result = (failwith "todo" : Result<int, string>)
let te_nested = ((1 + 2) : int)

// As a function argument
let te_arg = string (42 : int)

// ── For-loop pattern destructuring ──────────────────────────────────────────

let pairs = [(1, "a"); (2, "b")]

for (k, v) in pairs do
    printfn "%d %s" k v

for _ in [1; 2; 3] do
    printfn "tick"

let points = [{ X = 0; Y = 0 }; { X = 1; Y = 2 }]

for { X = x; Y = y } in points do
    printfn "(%d, %d)" x y

// ── Typed patterns ───────────────────────────────────────────────────────────

// In match arms
let tp_match (x: int) =
    match x with
    | (n : int) -> string n

// In function parameters
let tp_simple (x : int) = x * 2
let tp_tuple ((a, b) : int * int) = a + b
let tp_record ({ X = x; Y = y } : Point) = x + y

// In let bindings
let (n : int) = 42

// ── Unary ! (ref cell dereference) ──────────────────────────────────────────

let cell = ref 0
let v = !cell
let double_deref r s = !r + !s
cell.Value <- !cell + 1

// ── Secondary constructors ───────────────────────────────────────────────────

type Vec2(x: float, y: float) =
    new() = Vec2(0.0, 0.0)
    new(v: float) = Vec2(v, v)
    member _.X = x
    member _.Y = y

// With then clause — side-effects after delegation
type TrackedPoint(x: int, y: int) =
    new(x: int) =
        TrackedPoint(x, 0)
        then printfn "TrackedPoint(%d, 0)" x
    member _.X = x
    member _.Y = y

// Access modifier on secondary constructor
type Connection2(host: string, port: int) =
    private new() = Connection2("localhost", 5432)
    new(host: string) = Connection2(host, 5432)
    member _.Endpoint = sprintf "%s:%d" host port

// ── OOP-style tuple parameters in members ────────────────────────────────────

// Multi-parameter members using tuple (OOP) calling convention
type Calculator() =
    member this.Add(x: int, y: int) = x + y
    member this.Sub(x: int, y: int) = x - y
    static member Mul(a: float, b: float) = a * b

// Single typed parameter (equivalent to typed_pattern but via tuple_params)
type Formatter() =
    member this.Format(value: int) = string value
    member this.Pad(s: string, width: int) = s.PadLeft(width)

// Optional parameters in member methods
type Query() =
    member this.Run(sql: string, ?timeout: int) = sprintf "'%s' timeout=%d" sql (defaultArg timeout 30)

// ── static let / static do ───────────────────────────────────────────────────

// static let runs once when the type is first used (class-level value)
type Counter2() =
    static let mutable count = 0
    static let defaultName = "unnamed"
    static do printfn "Counter type initialized"
    member _.Increment() = count <- count + 1
    member _.Count = count
    member _.DefaultName = defaultName

// static let mutable with a type annotation
type Cache<'T>() =
    static let mutable store: Map<string, 'T> = Map.empty
    static do printfn "Cache initialized"
    member _.Set key value = store <- Map.add key value store
    member _.Get key = Map.tryFind key store

// ── Delegate type declarations ────────────────────────────────────────────────

type StringMapper = delegate of string -> string
type IntToString = delegate of int -> string
type Action = delegate of unit -> unit
type BinaryOp = delegate of (int * int) -> int
type Transformer<'a, 'b> = delegate of 'a -> 'b

// ── and! in computation expressions ──────────────────────────────────────────

// // Parallel applicative binding — two tasks run concurrently
// let parallelResult =
//     async {
//         let! a = async { return 1 }
//         and! b = async { return 2 }
//         return a + b
//     }

// // Multiple and! clauses
// let parallelTriple =
//     async {
//         let! x = async { return "hello" }
//         and! y = async { return 42 }
//         and! z = async { return true }
//         return x, y, z
//     }

// // and! with tuple pattern destructuring
// let parallelDestructure =
//     async {
//         let! (a, b) = async { return (1, 2) }
//         and! c = async { return 3 }
//         return a + b + c
//     }

// ── Struct tuples ─────────────────────────────────────────────────────────────

// struct tuple expressions
let st_pair = struct (1, 2)
let st_mixed = struct ("hello", 42)

// struct tuple types
type Point3D =
    struct
        val x: float
        val y: float
        val z: float
    end

let st_typed (x: struct (int * string)) = x

// struct tuple pattern in match
let st_describe t =
    match t with
    | struct (0, _) -> "zero first"
    | struct (a, b) -> sprintf "%d, %d" a b

// struct tuple destructure in let
let struct (sx, sy) = struct (10, 20)

let hasFocused (tests: string list) =
    let rec check test =
        true

    List.exists check tests

// ── SRTP member constraints ────────────────────────────────────────────────────

// Instance member constraint
let inline getName< ^T when ^T : (member Name: string)> (x: ^T) =
    failwith "srtp"

// Static member constraint
let inline add3< ^T when ^T : (static member (+): ^T * ^T -> ^T)> (a: ^T) (b: ^T) =
    failwith "srtp"

// Multiple constraints with and
let inline combined< ^T when ^T : (member Name: string) and ^T : (member Age: int)> (x: ^T) =
    failwith "srtp"

// Operator constraint
let inline add4< ^T when ^T : (static member op_Addition: ^T * ^T -> ^T)> (a: ^T) (b: ^T) =
    failwith "srtp"

// ── SRTP call-sites ──────────────────────────────────────────────────────────

// Calling an SRTP-constrained member. The `(^T : (member …) arg)` form lets
// inline functions actually invoke a statically resolved member.
let inline getXX< ^T when ^T : (member X: int)> (a: ^T) : int =
    (^T : (member X: int) a)

// Static-member SRTP call: argument is a parenthesized tuple.
let inline addThem< ^T when ^T : (static member (+): ^T * ^T -> ^T)> (a: ^T) (b: ^T) =
    (^T : (static member (+): ^T * ^T -> ^T) (a, b))

// Method call with parens-tuple argument.
let inline setIt< ^T when ^T : (member Set: int -> unit)> (a: ^T) (v: int) =
    (^T : (member Set: int -> unit) (a, v))

// ── Code quotations ────────────────────────────────────────────────────────────

// Typed quotation: Expr<'T>
let q_int = <@ 1 + 2 @>
let q_fn = <@ fun x -> x + 1 @>
let q_let = <@ let x = 10 in x * 2 @>
let q_if = <@ if true then 1 else 0 @>

// Untyped quotation: Expr
let q_raw = <@@ printfn "hello" @@>
let q_raw2 = <@@ 42 @@>

// Nested: quotation inside a function
let makeQuotation () = <@ 1 + 2 @>

// ── namespace rec ─────────────────────────────────────────────────────────────

// (namespace rec is only valid at file scope; demonstrated as a comment here
//  since this file already has a module declaration at line 2)
// namespace rec MyApp.Domain

// ── Object expression without 'with' ─────────────────────────────────────────

// Minimal object expression — no member overrides
let obj_no_with =
    { new System.IDisposable with
        member _.Dispose() = ()
    }

// IComparable with just the one required member, no extra members
let obj_comparable =
    { new System.IComparable with
        member _.CompareTo(_) = 0
    }

// ── Optional named argument reference (?ident) ───────────────────────────────

// ?param = optValue passes an already-wrapped option to an optional parameter
let maybeTimeout: int option = Some 60
let _opt_call = Query().Run("SELECT 1", ?timeout = maybeTimeout)

// ── Address-of (&x) ──────────────────────────────────────────────────────────

// &x passes a byref/ref argument; typical use with interop/threading APIs
let mutable addr_n = 0
let _addr_result = System.Threading.Interlocked.Increment(&addr_n)

// ── sizeof / typeof / typedefof ───────────────────────────────────────────────

let size_int    = sizeof<int>
let size_float  = sizeof<float>
let type_int    = typeof<int>
let type_string = typeof<string>
let typedef_list = typedefof<int list>
let typedef_map  = typedefof<Map<string, int>>


type HolderWithDefault() =
    [<DefaultValue>]
    val mutable Score: int
    [<DefaultValue>] val mutable A: int

/// OSC 8 hyperlink: clicking opens the file at the given line in the terminal.
/// The visible text uses a relative path; the URL uses the absolute path.
let fileLink (filePath: string) (lineNumber: int) =
    let url = $"file://%s{filePath}:%d{lineNumber}"
    let cwd = ""

    let rel =
        if filePath.StartsWith(cwd) then
            filePath.[cwd.Length + 1 ..]
        else
            filePath

    let display = $"%s{rel}:%d{lineNumber}"
    sprintf "\x1b]8;;%s\x1b\\%s\x1b]8;;\x1b\\" url display

let withTimeout (ms: int) (computation: Async<unit>) : Async<unit> =
    Async.FromContinuations(fun (resolve, reject, _cancel) ->
        let mutable settled = false
        let mutable timerId: obj = null

        ()
    )

type Empty =
    class end

type IEmpty =
    interface end

type Runner =

    static member runTestsWith() =
        let duplicates = ()

        if not true then
            ()

            for dup in [] do
                ()

            async { return 1 } |> ignore
        else

            let test = ()
            ()

// See https://github.com/ionide/ionide-fsgrammar/issues/177
// Check that custom operators definition that use any number of `/` are not captured as a comment
let (!//!) x y = x + y
let (%//%) x y = x + y
let (&//&) x y = x + y
let (+//+) x y = x + y
let (-//-) x y = x + y
let (.//.) x y = x + y
let (<//<) x y = x + y
let (=//=) x y = x + y
let (>//>) x y = x + y
let (?//?) x y = x + y
let (@//@) x y = x + y
let (^//^) x y = x + y
let (|//|) x y = x + y
let (<//>) x y = x + y

let (!///!) x y = x + y
let (%///%) x y = x + y
let (&///&) x y = x + y
let (+///+) x y = x + y
let (-///-) x y = x + y
let (.///.) x y = x + y
let (<///<) x y = x + y
let (=///=) x y = x + y
let (>///>) x y = x + y
let (?///?) x y = x + y
let (@///@) x y = x + y
let (^///^) x y = x + y
let (|///|) x y = x + y
let (<///>) x y = x + y
// Works for any number of `/`
let (</////////>) x y = x + y

// // Check that custom operators usage that use `//` is not captured as a comment
let add1 x y = x (!//!) y
let add2 x y = x (%//%) y
let add3_ x y = x (&//&) y
let add4_ x y = x (+//+) y
let add5 x y = x (-//-) y
let add6 x y = x (.//.) y
let add7 x y = x (<//<) y
let add8 x y = x (=//=) y
let add9 x y = x (>//>) y
let add10 x y = x (?//?) y
let add11 x y = x (@//@) y
let add12 x y = x (^//^) y
let add13 x y = x (|//|) y
let add14 x y = x (<//>) y

let add15 x y = x (!///!) y
let add16 x y = x (%///%) y
let add17 x y = x (&///&) y
let add18 x y = x (+///+) y
let add19 x y = x (-///-) y
let add20 x y = x (.///.) y
let add21 x y = x (<///<) y
let add22 x y = x (=///=) y
let add23 x y = x (>///>) y
let add24 x y = x (?///?) y
let add25 x y = x (@///@) y
let add26 x y = x (^///^) y
let add27 x y = x (|///|) y
let add28 x y = x (<///>) y
// Works for any number of `/`
let add29 x y = x (</////////>) y

type NameRecord =
    { Firstname : string
      Surname : string
      Notify : unit -> unit }

type NestedRecord =
    { Nested : NestedRecord
      PropB : string }

type private FancyClass2 (?thing:int) =
    class end

let testRecordColoration =
    { Firstname = "string" // Comments should work here
      Surname = ""
      Notify = fun _ -> ()
    }

let test () =
    Map.empty<string, _>

type FancyClass(thing:int, var2 : string -> string, ``ddzdz``: string list, extra) as xxx =

    static member (>) (v1 : int, v2 : int) = v1 > v2
    static member (<) (v1 : int, v2 : int) = v2 < v2
    static member (< ) (v1 : int, v2 : int) = v2 < v2
    static member (<|>) (v1 : int, v2 : int) = v2 < v2

    member inline _.Q () = 3

type MyType<'``Generic type with spaces``, 'T>() =
    let mutable myInt1 = 10
    static let mutable myInt3 = 3
    [<DefaultValue>] static val mutable private myInt2 : int
    [<DefaultValue>] val mutable myString : '``Generic type with spaces``
    [<DefaultValue>] val mutable myString2 : 'T

let parensFoo2 = ( )
let parensFoo3 = (1)

type ParensBaz1() = class end
type ParensBaz2( ) = class end
type ParensBaz3(x) = class end

let toString = function
| North -> "North"
| South -> "South"
| _ -> "Horizontal"

module Direction =
    let toString = function
    | North -> "North"
    | South -> "South"
    | _ -> "Horizontal"


type RequiredName =
    private
    | RequiredName of rn: string

    member this.Value = let (RequiredName v) = this in v

type Email =
    private { Address: string }

type PrivateColor = private | Red | Green | Blue

type internal Token =
    internal
    | Ident of string
    | Number of int

// Bare first union case (omitted leading `|`): `Generated` must color like
// `UserInput`/`SharedReference`, not land in an ERROR node.
type Origin = Generated | UserInput | SharedReference

// Single-case union with fields parses as a union (constructor), not an alias.
type Change = Renamed of Name
