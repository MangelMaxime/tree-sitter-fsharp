let add a b = a + b
// <- keyword
//  ^ function
//      ^ variable.parameter
//        ^ variable.parameter
//              ^ operator

let result = items |> List.map describe
//  ^ function
//                 ^ operator
//                          ^ function

type Shape =
// <- keyword
//   ^ type
    | Circle of float
    //  ^ constructor
    //          ^ type
    | Point
    //  ^ constructor

/// A documented binding.
// <- comment.line.documentation
let documented = 1
//               ^ constant.numeric.integer

[<Obsolete("msg")>]
// ^ attribute
let tagged () = "text"
//              ^ string

#if DEBUG
// <- keyword.directive
let dbg = true
//        ^ constant.builtin.boolean
#else
// <- keyword.directive
let dbg = false
#endif
// <- keyword.directive

module Helpers =
// <- keyword
//     ^ namespace
    let inline private combine (x: int) = x
    // <- keyword
    //  ^ keyword
    //         ^ keyword.control.access
    //                          ^ variable.parameter
    //                              ^ type

register ("HelloWorld", helloWorld)
// <- function
register ("Counter", counter)
// <- function
register ("Checkbox", checkbox)
// <- function
