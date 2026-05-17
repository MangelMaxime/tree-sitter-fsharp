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
let valule_string = "str"
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
    elif "c" then
        "c"
    else
        "nothing"

Environment.CurrentDirectory |> printfn "%A"

let r = Regex.Escape "dw"
