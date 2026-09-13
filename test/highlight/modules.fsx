// Modules and namespaces: declaration keywords are keyword.storage.type (D16),
// open is keyword.control.import (D13), every segment of a namespace, module
// or open path is a namespace, and a capitalised qualifier of a call or of a
// value is a namespace with the lowercase value itself a function (D3).

namespace My.App
// <- keyword.storage.type
//        ^ namespace
//          ^ punctuation
//           ^ namespace

open System
// <- keyword.control.import
//   ^ namespace

open System.IO
//   ^ namespace
//         ^ punctuation
//          ^ namespace

open type System.Math
//   ^ keyword.storage.type
//        ^ namespace
//               ^ namespace

open global.System.Text
//   ^ namespace
//          ^ namespace
//                 ^ namespace

module Helpers =
// <- keyword.storage.type
//     ^ namespace
//             ^ operator

    let twice x = x * 2
//  ^ keyword
//      ^ function

    module Nested =
//  ^ keyword.storage.type
//         ^ namespace

        let inner = 1
//          ^ function

module Abbrev = System.Text.RegularExpressions
//     ^ namespace
//              ^ namespace
//                     ^ namespace
//                          ^ namespace

[<RequireQualifiedAccess>]
//^ attribute
module Q =
//     ^ namespace
    let value = 1

[<AutoOpen>]
//^ attribute
module Auto =
    let opened = 2

exception MyError of string
// <- keyword.storage.type
//        ^ type
//                ^ keyword
//                   ^ type.builtin

module Uses =

    let a = Helpers.twice 2
//          ^ namespace
//                  ^ function

    let b = Helpers.Nested.inner
//          ^ namespace
//                  ^ namespace
//                         ^ function

    let c = Q.value
//          ^ namespace
//            ^ function

    let d = System.IO.Path.Combine("a", "b")
//          ^ namespace
//                 ^ namespace
//                    ^ namespace
//                         ^ function

    let e = Abbrev.Regex("x")
//          ^ namespace
//                 ^ function

    let f = List.map Helpers.twice [ 1; 2 ]
//          ^ namespace
//               ^ function
//                   ^ namespace
//                           ^ function
