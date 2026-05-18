namespace MyProject.Core

[<AutoOpen>]
module Utils =
    let add a b = a + b
    let sub a b = a - b

module private Internals =
    let secret = 42

module rec MutuallyRecursive =
    let isEven n = if n = 0 then true else isOdd (n - 1)
    let isOdd n = if n = 0 then false else isEven (n - 1)

// File-level module (no =, rest of file is body)
// namespace global
