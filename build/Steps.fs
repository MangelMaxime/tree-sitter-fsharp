/// The generate and build steps every command that loads a parser runs first.
module EasyBuild.Steps

open System.IO
open EasyBuild.Tools

let private signature = Path.Combine(root, "signature")

let private at (parts: string list) =
    Path.Combine(List.toArray (root :: parts))

let generate (announce: bool) (force: bool) =
    incremental
        "generate"
        announce
        force
        [ at [ "grammar.js" ]; cliVersion ]
        [ at [ "src"; "parser.c" ]; at [ "src"; "grammar.json" ]; at [ "src"; "node-types.json" ] ]
        (fun () -> treeSitter root [ "generate"; "grammar.js" ])

    incremental
        "generate-signature"
        announce
        force
        [ at [ "signature"; "grammar.js" ]; at [ "grammar.js" ]; cliVersion ]
        [
            at [ "signature"; "src"; "parser.c" ]
            at [ "signature"; "src"; "grammar.json" ]
            at [ "signature"; "src"; "node-types.json" ]
        ]
        (fun () -> treeSitter signature [ "generate" ])

let build (announce: bool) (force: bool) =
    generate announce force

    incremental
        "build"
        announce
        force
        [ at [ "src"; "parser.c" ]; at [ "src"; "scanner.c" ]; cliVersion ]
        [ at [ "parser.so" ] ]
        (fun () -> treeSitter root [ "build"; "--output"; "parser.so" ])

    incremental
        "build-signature"
        announce
        force
        [
            at [ "signature"; "src"; "parser.c" ]
            at [ "signature"; "src"; "scanner.c" ]
            at [ "src"; "scanner.c" ]
            cliVersion
        ]
        [ at [ "signature"; "parser.so" ] ]
        (fun () -> treeSitter root [ "build"; "--output"; "signature/parser.so"; "signature" ])
