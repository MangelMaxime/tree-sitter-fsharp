let total = Seq.sum values
//             ^ punctuation

let formatted = $"%d{count}"
//                ^ string.special

let handler = use r = open_ ()
//                ^ variable

let boom () = raise (exn "x")
//            ^ function.builtin

type Csv = CsvProvider<Sample=SampleFile>
//                            ^ constant
