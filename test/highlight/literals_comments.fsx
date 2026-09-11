// a line comment
// <- comment.line

(* a block comment *)
// <- comment.block

(** a doc block *)
// <- comment.block.documentation

let count = 42
//          ^ constant.numeric.integer

let ratio = 1.5
//          ^ constant.numeric.float

let initial = 'a'
//            ^ constant.character

let enabled = true
//            ^ constant.builtin.boolean

let nothing = ()
//            ^ constant.builtin

let absent = null
//           ^ constant.builtin

let greeting = $"hi {name}"
//                  ^ punctuation.special
