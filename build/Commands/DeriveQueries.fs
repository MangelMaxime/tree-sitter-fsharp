module EasyBuild.Commands.DeriveQueries

open System.ComponentModel
open System.IO
open System.Text
open System.Text.RegularExpressions
open Spectre.Console
open Spectre.Console.Cli
open EasyBuild
open EasyBuild.Workspace

let private helixPath = Workspace.queries.``highlights.scm``
let private nvimPath = Workspace.queries.nvim.``highlights.scm``
let private nvimExtraPath = Workspace.scripts.``nvim-highlights-extra.scm``
let private zedPath = Workspace.queries.zed.``highlights.scm``

// Neovim ignores captures that start with an underscore, so `_wildcard` renders as plain text.
let private nvimMap =
    Map
        [
            "keyword.control.access", "keyword.modifier"
            "keyword.control", "keyword"
            "keyword.directive", "keyword.directive"
            "keyword.operator", "keyword.operator"
            "comment.line.documentation", "comment.documentation"
            "comment.block.documentation", "comment.documentation"
            "comment.line", "comment"
            "comment.block", "comment"
            "constant.numeric.integer", "number"
            "constant.numeric.float", "number.float"
            "constant.builtin.boolean", "boolean"
            "constant.character", "character"
            "variable.other.member", "variable.member"
            "type.parameter", "type"
            "namespace", "module"
            "punctuation", "punctuation.delimiter"
            "wildcard", "_wildcard"
        ]

let private zedMap =
    Map
        [
            "keyword.control.access", "keyword"
            "keyword.control", "keyword"
            "keyword.operator", "keyword"
            "keyword.directive", "preproc"
            "comment.line.documentation", "comment.doc"
            "comment.block.documentation", "comment.doc"
            "comment.line", "comment"
            "comment.block", "comment"
            "constant.numeric.integer", "number"
            "constant.numeric.float", "number"
            "constant.builtin.boolean", "boolean"
            "constant.character", "constant"
            "variable.other.member", "property"
            "variable.builtin", "variable.special"
            "type.parameter", "type"
            "namespace", "type @namespace"
        ]

let private capture = Regex(@"@([A-Za-z_][\w.]*)")

let private mapCaptures (table: Map<string, string>) (text: string) =
    capture.Replace(
        text,
        fun m ->
            let name = m.Groups[1].Value
            "@" + (table.TryFind name |> Option.defaultValue name)
    )

let private anyOf = Regex(@"\(#match\? (@[\w.]+) ""\^\(([\w|]+)\)\$""\)")

// Neovim's `#match?` takes a Vim regex, so anchored alternations become `#any-of?` and the
// remaining character classes are valid Lua patterns.
let private nvimPredicates (text: string) =
    let text =
        anyOf.Replace(
            text,
            fun m ->
                let words =
                    m.Groups[2].Value.Split '|'
                    |> Array.map (fun w -> $"\"%s{w}\"")
                    |> String.concat " "

                $"(#any-of? %s{m.Groups[1].Value} %s{words})"
        )

    text.Replace("(#match?", "(#lua-match?")

let private stripComments (text: string) =
    text.Split '\n'
    |> Array.map (fun line ->
        let mutable inString = false
        let mutable cut = line.Length

        for i in 0 .. line.Length - 1 do
            if cut = line.Length then
                if line[i] = '"' && (i = 0 || line[i - 1] <> '\\') then
                    inString <- not inString
                elif line[i] = ';' && not inString then
                    cut <- i

        line.Substring(0, cut)
    )
    |> String.concat "\n"

let private normalise (text: string) =
    text.Split(
        [|
            ' '
            '\t'
            '\n'
            '\r'
        |],
        System.StringSplitOptions.RemoveEmptyEntries
    )
    |> String.concat " "

/// Top-level patterns of a query file, whitespace-normalised.
let patterns (text: string) =
    let result = ResizeArray<string>()
    let current = StringBuilder()
    let mutable depth = 0
    let mutable inString = false

    for ch in stripComments text do
        if inString then
            current.Append ch |> ignore

            if ch = '"' then
                inString <- false
        else
            if ch = '"' then
                inString <- true
            elif ch = '(' || ch = '[' then
                depth <- depth + 1
            elif ch = ')' || ch = ']' then
                depth <- depth - 1

            current.Append ch |> ignore

            if depth = 0 && (ch = ')' || ch = ']') then
                result.Add(normalise (current.ToString()))
                current.Clear() |> ignore

    let tail = normalise (current.ToString())

    if tail <> "" then
        for piece in Regex.Split(tail, "(?=@)") do
            if piece.Trim() <> "" then
                result[result.Count - 1] <- result[result.Count - 1] + " " + piece.Trim()

    result
    |> Seq.fold
        (fun (merged: string list) pattern ->
            match merged with
            | last :: rest when pattern.StartsWith '@' -> (last + " " + pattern) :: rest
            | _ -> pattern :: merged
        )
        []
    |> List.rev

let private renderNvim (helix: string) (extra: string) =
    let header =
        "; GENERATED by `task queries:derive` from ../highlights.scm - do not edit.\n"
        + "; Neovim capture names (nvim-treesitter conventions) and `#lua-match?`/`#any-of?`\n"
        + "; predicates; the Neovim-only refinements at the end come from\n"
        + "; scripts/nvim-highlights-extra.scm.\n\n"

    let body = helix |> mapCaptures nvimMap |> nvimPredicates
    header + body.TrimEnd '\n' + "\n\n" + extra.TrimEnd '\n' + "\n"

/// Lines of a unified-style diff between two sequences, empty when they are equal.
let private diff (want: string list) (have: string list) =
    let a = Array.ofList want
    let b = Array.ofList have
    let lcs = Array2D.zeroCreate (a.Length + 1) (b.Length + 1)

    for i in a.Length - 1 .. -1 .. 0 do
        for j in b.Length - 1 .. -1 .. 0 do
            lcs[i, j] <-
                if a[i] = b[j] then
                    lcs[i + 1, j + 1] + 1
                else
                    max lcs[i + 1, j] lcs[i, j + 1]

    let lines = ResizeArray()
    let mutable i = 0
    let mutable j = 0

    while i < a.Length || j < b.Length do
        if i < a.Length && j < b.Length && a[i] = b[j] then
            i <- i + 1
            j <- j + 1
        elif j < b.Length && (i = a.Length || lcs[i, j + 1] >= lcs[i + 1, j]) then
            lines.Add("+ " + b[j])
            j <- j + 1
        else
            lines.Add("- " + a[i])
            i <- i + 1

    List.ofSeq lines

/// Raw top-level chunks of a query file: each pattern with the comments above it.
let chunks (text: string) =
    let result = ResizeArray<string>()
    let mutable start = 0
    let mutable depth = 0
    let mutable inString = false
    let mutable inComment = false
    let mutable i = 0

    while i < text.Length do
        let ch = text[i]

        if inComment then
            if ch = '\n' then
                inComment <- false
        elif inString then
            if ch = '"' && text[i - 1] <> '\\' then
                inString <- false
        else
            match ch with
            | ';' -> inComment <- true
            | '"' -> inString <- true
            | '('
            | '[' -> depth <- depth + 1
            | ')'
            | ']' ->
                depth <- depth - 1

                if depth = 0 then
                    let mutable stop = i + 1

                    while stop < text.Length && text[stop] <> '\n' do
                        stop <- stop + 1

                    result.Add(text.Substring(start, stop - start))
                    start <- stop
            | _ -> ()

        i <- i + 1

    List.ofSeq result

let private signatureHeader (name: string) =
    $"; GENERATED by `task queries:derive` from ../%s{name} - do not edit.\n"
    + "; The patterns of the F# query that compile against the signature grammar.\n\n"

/// Writes queries/signature/*.scm and queries/zed/signature/*.scm: every pattern of the
/// Helix and Zed query files whose node types exist in the signature grammar.
let private deriveSignature (check: bool) =
    use language = Parser.loadAs Parser.Signature Parser.Signature.ParserPath
    let mutable status = 0

    let sources =
        [
            for dir in
                [
                    Workspace.queries.``.``
                    Workspace.queries.zed.``.``
                ] do
                for file in Directory.GetFiles(dir, "*.scm") |> Array.sort do
                    file, Path.Combine(dir, "signature")
        ]

    for source, directory in sources do
        Directory.CreateDirectory directory |> ignore
        let name = Path.GetFileName source

        let compiles (chunk: string) =
            try
                use _ = language.CreateQuery chunk
                true
            with _ ->
                false

        let kept =
            chunks (File.ReadAllText source)
            |> List.filter (fun chunk -> chunk.Trim() <> "" && compiles chunk)

        let output = signatureHeader name + (String.concat "" kept).Trim() + "\n"
        let target = Path.Combine(directory, name)
        let relative = Path.GetRelativePath(Workspace.``.``, target)

        if check then
            if not (File.Exists target) || File.ReadAllText target <> output then
                AnsiConsole.MarkupLineInterpolated
                    $"[red]{relative} is out of date[/]: run `task queries:derive`"

                status <- 1
            else
                printfn $"%s{relative} is up to date"
        else
            File.WriteAllText(target, output)
            printfn $"wrote %s{relative}"

    status

type DeriveQueriesSettings() =
    inherit CommandSettings()

    [<CommandOption("--check")>]
    [<Description("Fail when a derived file is out of date instead of rewriting it")>]
    member val Check = false with get, set

/// Derives queries/nvim/highlights.scm from the Helix one and checks the Zed one against it.
type DeriveQueriesCommand() =
    inherit Command<DeriveQueriesSettings>()
    interface ICommandLimiter<DeriveQueriesSettings>

    override _.Execute(_, settings, _) =
        let relative (path: string) =
            Path.GetRelativePath(Workspace.``.``, path)

        let helix = File.ReadAllText helixPath
        let nvim = renderNvim helix (File.ReadAllText nvimExtraPath)

        let nvimStatus =
            if settings.Check then
                if File.ReadAllText nvimPath <> nvim then
                    AnsiConsole.MarkupLineInterpolated
                        $"[red]{relative nvimPath} is out of date[/]: run `task queries:derive`"

                    1
                else
                    printfn $"%s{relative nvimPath} is up to date"
                    0
            else
                File.WriteAllText(nvimPath, nvim)
                printfn $"wrote %s{relative nvimPath}"
                0

        let zedStatus =
            match
                diff (patterns (mapCaptures zedMap helix)) (patterns (File.ReadAllText zedPath))
            with
            | [] ->
                printfn $"%s{relative zedPath} matches %s{relative helixPath}"
                0
            | lines ->
                AnsiConsole.MarkupLineInterpolated
                    $"[red]{relative zedPath} has drifted from {relative helixPath}[/] (- expected from Helix, + found in Zed):"

                for line in lines do
                    printfn "%s" line

                1

        let signatureStatus = deriveSignature settings.Check
        max nvimStatus (max zedStatus signatureStatus)
