// Signature files: declaration keywords, val, constructor and member signatures.

namespace Sample
// <- keyword.storage.type
//        ^ namespace

open System
// <- keyword.control.import

type Shape =
// <- keyword.storage.type
//   ^ type
    | Circle of radius: float
//           ^ keyword

module Geometry =
// <- keyword.storage.type

    val area: shape: Shape -> float
//  ^ keyword

type Counter =
    new: unit -> Counter
//  ^ keyword
    member Value: int
//  ^ keyword
