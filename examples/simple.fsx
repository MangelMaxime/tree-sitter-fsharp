open System
open System.Text.RegularExpressions
open type System.Math

// This is a comment

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

let add a b = a + b

Environment.CurrentDirectory |> printfn "%A"

let r = Regex.Escape "dw"
