open System
open System.Text.RegularExpressions

printfn "Hello"

let answer = 42

let add a b = a + b

Environment.CurrentDirectory |> printfn "%A"

let r = Regex.Escape "dw"
