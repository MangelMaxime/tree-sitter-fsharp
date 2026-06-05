module LocalsTest

// ── Function parameters ──
// x, y → parameter colour at their use-sites in the body
let add x y =
    x + y

// ── Nested let-bound value ──
// total → @function (let-name colour) where it's used
let compute () =
    let total = 10
    total + 1

// ── Lambda parameter ──
// n → parameter colour in n + 1
let mapper = List.map (fun n -> n + 1)

// ── For-loop variable ──
// item → @variable (grey); xs → parameter
let printAll xs =
    for item in xs do
        printfn "%A" item

// ── Match-arm pattern binding ──
// v → matches its binding; Some/None → constructor; opt → parameter
let unwrap opt =
    match opt with
    | Some v -> v
    | None -> 0

// ── Member parameter ──
// n → parameter colour in n + 1
type Counter() =
    member _.AddTo n = n + 1

// ── Escape tests: a binding must NOT leak out of its scope ──
// Each second use is outside the binding's scope → must stay PLAIN
// (a leak would show as the binding's colour).

// member param, not visible in a sibling member
type EscapeMember() =
    member _.A value = value     // value → parameter (red)
    member _.B count = value     // value → PLAIN (leak ⇒ red)

// function param, not visible in another function
let escA p = p                   // p → parameter (red)
let escB q = p                   // p → PLAIN (leak ⇒ red)

// lambda param, not visible after the lambda
let escLambda =
    let f = fun z -> z           // z → parameter (red)
    z                            // z → PLAIN (leak ⇒ red)

// nested let-name, not visible in another function
let escLetA () =
    let secret = 42              // secret → @function (blue)
    secret                       // secret → @function (blue)
let escLetB () = secret          // secret → PLAIN (leak ⇒ blue)

// for-loop variable, not visible after the loop
let escFor xs =
    for w in xs do
        printfn "%A" w           // w → @variable (grey)
    w                            // w → PLAIN (grey)

// match-arm binding, not visible in another arm
let escMatch x =
    match x with
    | Some hit -> hit            // hit → @variable (grey)
    | None -> hit                // hit → PLAIN (grey)
