let describe x =
    match x with
//  ^ keyword.control
    | Some v -> v
//  ^ keyword.control
    | None -> 0

let check a =
    if a then 1 else 2
//  ^ keyword.control
//         ^ keyword.control

let cast (o: obj) = o :?> string
//                    ^ operator

let negated b = not b
//              ^ keyword.operator

let item = list.Head
//              ^ variable.other.member

let pair: int * string = (1, "a")
//        ^ type.builtin
//            ^ keyword
//              ^ type.builtin

type Box<'T> = { Value: 'T }
//       ^ type.parameter

type Counter() =
    member this.Value = 1
//         ^ variable.builtin
    member _.Other = 2
//         ^ wildcard

let nums = [ 1; 2 ]
//         ^ punctuation.bracket
//            ^ punctuation.delimiter
