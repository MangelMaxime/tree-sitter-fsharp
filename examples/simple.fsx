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
let valule_triple_multiline = """
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
    f (if a then b else 0)

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
let arith_prec = 1 + 2 * 3        // 1 + (2 * 3) = 7
let arith_paren = (1 + 2) * 3     // (1 + 2) * 3 = 9

// Boolean
let bool_and = true && false
let bool_or = true || false
let bool_prec a b c d = a > b && c > d    // (a > b) && (c > d)
let bool_short a b c = a || b && c      // a || (b && c)

// Pipe
let pipe_right = [1; 2; 3] |> List.length
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

