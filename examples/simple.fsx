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
type Direction =
    | North = 0
    | South = 1
    | East = 2
    | West = 3

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
    | JArray of JsonArray
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
