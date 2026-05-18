// File-level module declaration (no = sign, rest of file is its body)
module ScriptHelpers

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
let valule_char = 'c'
let valule_char_escape = '\n'
let valule_char_decimal = '\097'
let valule_char_hex = '\x41'
let valule_char_unicode = 'A'
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

type MyOption<'a> =
    | Some of 'a
    | None

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
        A : 'A
        B : 'A
    }

// Attributes
[<Obsolete("use newAdd instead")>]
let oldAdd x y = x + y

[<AutoOpen>]
[<RequireQualifiedAccess>]
type AttributedUnion =
    | X
    | Y

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

type Wrapper<'T>(value: 'T) =
    member this.Value = value
    member this.Map (f: 'T -> 'T) = Wrapper(f value)

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

// _ as self identifier (no self reference needed)
type Singleton() =
    static member Instance = Singleton()
    override _.ToString() = "Singleton"

// Optional primary constructor parameter
type Connection(?host: string, ?port: int) =
    member _.Host = defaultArg host "localhost"
    member _.Port = defaultArg port 5432

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

// use / use! inside CE
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
