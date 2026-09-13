// Bindings: every let name is a function (D1, D2), tuple-destructured names
// included; a use name is a variable; an operator name is an operator (D23);
// parameters are variable.parameter; a called member is function.method (D4);
// modifiers and access keywords
// are keyword.storage.modifier (D17); a [<Literal>] declaration is a constant
// (D19). Plain (no capture): value references in bodies.

let value = 42
// <- keyword
//  ^ function
//        ^ operator
//          ^ constant.numeric.integer

let add x y = x + y
//  ^ function
//      ^ variable.parameter
//        ^ variable.parameter
//              ^ operator

let inline twice (f: int -> int) (x: int) = f (f x)
//  ^ keyword.storage.modifier
//         ^ function
//                ^ variable.parameter
//                   ^ type.builtin
//                       ^ keyword
//                                ^ variable.parameter
//                                             ^ function

let mutable counter = 0
//  ^ keyword.storage.modifier
//          ^ function

let private secret = 1
//  ^ keyword.storage.modifier
//          ^ function

let rec fact n = if n = 0 then 1 else n * fact (n - 1)
//  ^ keyword.storage.modifier
//      ^ function
//           ^ variable.parameter
//                                        ^ function

let rec isEven n = n = 0 || isOdd (n - 1)
//      ^ function
//                          ^ function
and isOdd n = n <> 0 && isEven (n - 1)
// <- keyword
//  ^ function
//        ^ variable.parameter
and (|IsZero|_|) n = if n = 0 then Some () else None
//  ^ function

let (|Even|Odd|) n = if n % 2 = 0 then Even else Odd
//  ^ function
//                                     ^ constructor
//                                               ^ constructor

let (+++) a b = a + b
//   ^ operator
//        ^ variable.parameter

let ``quoted name`` = 1
//  ^ function

let pair: int * string = (1, "a")
//  ^ function
//      ^ punctuation.delimiter
//        ^ type.builtin
//            ^ keyword
//              ^ type.builtin

let typed (x: int) : string = string x
//  ^ function
//                   ^ type.builtin
//                            ^ function.builtin

let first, second = 1, 2
//  ^ function
//         ^ function
//       ^ punctuation.delimiter

let (a, b) = (3, 4)
//   ^ function
//      ^ function

let read path =
//  ^ function
//       ^ variable.parameter
    use file = File.OpenRead path
//  ^ keyword
//      ^ variable
//             ^ namespace
//                  ^ function
    file.Flush()
//       ^ function.method
    let length = file.Length in length
//  ^ keyword
//      ^ function
//                    ^ variable.other.member
//                           ^ keyword

[<Literal>] let MaxSize = 10
//^ attribute
//              ^ constant

do printfn "start"
// <- keyword
// ^ function
