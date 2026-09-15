module EasyBuild.Commands.ZedExtension

open System
open System.ComponentModel
open System.IO
open System.Text.RegularExpressions
open Spectre.Console
open Spectre.Console.Cli
open EasyBuild.Tools
open EasyBuild.Commands.Dev

type ZedExtensionSettings() =
    inherit CommandSettings()

    [<CommandOption("--rev <SHA>")>]
    [<Description("Grammar commit to pin (default: HEAD of this repository)")>]
    member val Rev = "" with get, set

    [<CommandOption("--version <VERSION>")>]
    [<Description("Released version, used in the branch name and the pull request title (default: tree-sitter.json)")>]
    member val Version = "" with get, set

    [<CommandOption("--fork <OWNER/NAME>")>]
    [<Description("Repository the update branch is pushed to (default MangelMaxime/zed-fsharp)")>]
    member val Fork = "MangelMaxime/zed-fsharp" with get, set

    [<CommandOption("--upstream <OWNER/NAME>")>]
    [<Description("Repository the pull request targets (default nathanjcollins/zed-fsharp)")>]
    member val Upstream = "nathanjcollins/zed-fsharp" with get, set

    [<CommandOption("--dry-run")>]
    [<Description("Clone, apply and commit locally; print the push and the pull request instead of doing them")>]
    member val DryRun = false with get, set

let private branch = "tree-sitter-fsharp/update"

/// Grammar names from `[grammars.<name>]` sections whose repository is this grammar.
let private ourGrammars (manifest: string) =
    [
        for m in Regex.Matches(manifest, @"^\[grammars\.([\w-]+)\]\s*\n((?:(?!^\[).*\n?)*)", RegexOptions.Multiline) do
            if m.Groups[2].Value.Contains "tree-sitter-fsharp" then
                m.Groups[1].Value
    ]

/// Rewrites the `commit`/`rev` line of every `[grammars.<name>]` section for our grammars.
let private pinGrammars (manifest: string) (rev: string) =
    let sections = ourGrammars manifest

    let lines = manifest.Split '\n'
    let mutable current = ""

    let updated =
        lines
        |> Array.map (fun line ->
            let section = Regex.Match(line, @"^\[grammars\.([\w-]+)\]")

            if section.Success then
                current <- section.Groups[1].Value
                line
            elif line.StartsWith '[' then
                current <- ""
                line
            elif List.contains current sections then
                Regex.Replace(line, "^(commit|rev)(\\s*=\\s*)\"[0-9a-f]+\"", $"$1$2\"%s{rev}\"")
            else
                line
        )

    sections, String.Join('\n', updated)

/// The `languages/<dir>` whose config.toml names the grammar.
let private languageDirectory (checkout: string) (grammar: string) =
    Directory.GetDirectories(Path.Combine(checkout, "languages"))
    |> Array.tryFind (fun dir ->
        let config = Path.Combine(dir, "config.toml")
        File.Exists config && Regex.IsMatch(File.ReadAllText config, $"^grammar\\s*=\\s*\"%s{grammar}\"" , RegexOptions.Multiline)
    )

/// Pins the grammar commit and copies the queries into a clone of the Zed extension, then
/// opens the pull request from the `tree-sitter-fsharp/update` branch or updates the open one.
type ZedExtensionCommand() =
    inherit Command<ZedExtensionSettings>()
    interface ICommandLimiter<ZedExtensionSettings>

    override _.Execute(_, settings, _) =
        let rev =
            if settings.Rev <> "" then settings.Rev
            else gitRead root [ "rev-parse"; "HEAD" ]

        let version =
            if settings.Version <> "" then settings.Version.TrimStart 'v'
            else Regex.Match(File.ReadAllText(Path.Combine(root, "tree-sitter.json")), "\"version\"\\s*:\\s*\"([^\"]+)\"").Groups[1].Value

        let checkout = Path.Combine(Path.GetTempPath(), "zed-fsharp-" + Guid.NewGuid().ToString "N")
        run "gh" root [ "repo"; "clone"; settings.Fork; checkout ]
        git checkout [ "fetch"; "-q"; $"https://github.com/%s{settings.Upstream}.git"; "main" ]
        git checkout [ "checkout"; "-q"; "-B"; branch; "FETCH_HEAD" ]

        let manifestPath = Path.Combine(checkout, "extension.toml")
        let grammars, manifest = pinGrammars (File.ReadAllText manifestPath) rev

        if grammars.IsEmpty then
            failwith $"no [grammars.*] section of %s{settings.Upstream} points at tree-sitter-fsharp"

        File.WriteAllText(manifestPath, manifest)

        for grammar in grammars do
            // The dev extension names the signature language directory `fsharp-signature`.
            let language = if grammar = "fsharp_signature" then "fsharp-signature" else grammar

            match languageDirectory checkout grammar with
            | Some target -> copyZedQueries language target
            | None -> printfn $"no languages/*/config.toml uses grammar %s{grammar}: queries not copied"

        git checkout [ "add"; "-A" ]

        if gitCode checkout [ "diff"; "--cached"; "--quiet" ] = 0 then
            printfn $"%s{settings.Upstream} already pins %s{rev} with the current queries"
            0
        else
            let short = rev.Substring(0, 7)
            let title = $"chore: update tree-sitter-fsharp to %s{version}"

            let pinned = grammars |> List.map (fun grammar -> $"`%s{grammar}`") |> String.concat " and "

            let body =
                String.concat
                    "\n"
                    [
                        $"Pins %s{pinned} to [`%s{short}`](https://github.com/MangelMaxime/tree-sitter-fsharp/commit/%s{rev}) (release [v%s{version}](https://github.com/MangelMaxime/tree-sitter-fsharp/releases/tag/v%s{version})) and copies the matching Zed queries."
                        ""
                        "Opened by the `zed-extension` workflow of tree-sitter-fsharp; every release updates this pull request until it is merged."
                    ]

            git checkout [ "-c"; "user.name=tree-sitter-fsharp"; "-c"; "user.email=noreply@github.com"; "commit"; "-qm"; $"%s{title} (%s{short})" ]
            printfn "%s" (gitRead checkout [ "show"; "--stat"; "--format=%s"; "HEAD" ])

            if settings.DryRun then
                printfn $"dry run: would push %s{branch} to %s{settings.Fork} and open or update the pull request on %s{settings.Upstream} \"%s{title}\":\n\n%s{body}\n"
                printfn $"clone kept at %s{checkout}"
                0
            else
                git checkout [ "push"; "--force"; "-u"; "origin"; branch ]

                let upstream = [ "--repo"; settings.Upstream ]
                let owner = settings.Fork.Split('/')[0]

                let existing =
                    read
                        "gh"
                        checkout
                        ([
                            "pr"
                            "list"
                            "--head"
                            branch
                            "--state"
                            "open"
                            "--json"
                            "number,headRepositoryOwner"
                            "--jq"
                            $".[] | select(.headRepositoryOwner.login == \"%s{owner}\") | .number"
                         ]
                         @ upstream)

                if existing = "" then
                    run
                        "gh"
                        checkout
                        ([ "pr"; "create"; "--base"; "main"; "--head"; $"%s{owner}:%s{branch}"; "--title"; title; "--body"; body ]
                         @ upstream)
                else
                    run "gh" checkout ([ "pr"; "edit"; existing; "--title"; title; "--body"; body ] @ upstream)
                    printfn $"updated pull request #%s{existing} on %s{settings.Upstream}"

                Directory.Delete(checkout, true)
                0
