let choose flag = if flag then Some 1 else None
//                                            ^ constructor
//                             ^ constructor

let style = Aligned, ValueNone
//          ^ constructor
//                   ^ constructor

let g (?opt: int) = opt
//     ^ operator
//      ^ variable.parameter

let h v = match v with | :? string as s -> s | _ -> ""
//                                    ^ variable

let first = arr.[0]
//             ^ punctuation.bracket

let q = <@ %%e @>
//         ^ operator

namespace global
//        ^ namespace
