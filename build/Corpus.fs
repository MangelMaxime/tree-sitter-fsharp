/// The pinned real-world repositories every sweep measures.
module EasyBuild.Corpus

open System
open System.IO
open SimpleExec
open EasyBuild.Workspace

type Repo =
    {
        Name: string
        Url: string
        Sha: string
        Subdirectory: string option
    }

type SourceFile =
    {
        Repo: Repo
        Path: string
        /// Path relative to the corpus directory, with forward slashes.
        Relative: string
    }

let directory =
    match Environment.GetEnvironmentVariable "FSHARP_BENCH_DIR" with
    | null
    | "" ->
        Path.Combine(
            Environment.GetFolderPath Environment.SpecialFolder.UserProfile,
            ".cache",
            "fsharp-grammar-bench"
        )
    | dir -> dir

let manifest () =
    File.ReadLines Workspace.scripts.``bench-manifest.txt``
    |> Seq.map _.Trim()
    |> Seq.filter (fun line -> line <> "" && not (line.StartsWith '#'))
    |> Seq.map (fun line ->
        match line.Split(' ', StringSplitOptions.RemoveEmptyEntries) with
        | [| name; url; sha |] ->
            {
                Name = name
                Url = url
                Sha = sha
                Subdirectory = None
            }
        | [| name; url; sha; subdirectory |] ->
            {
                Name = name
                Url = url
                Sha = sha
                Subdirectory = Some subdirectory
            }
        | _ -> failwith $"Malformed manifest line: %s{line}"
    )
    |> List.ofSeq

let private clone (repo: Repo) =
    let destination = Path.Combine(directory, repo.Name)
    Directory.CreateDirectory destination |> ignore

    let git (arguments: string) =
        Command.Run("git", arguments, workingDirectory = destination, noEcho = true)

    git "init -q"
    git $"remote add origin %s{repo.Url}"
    git $"fetch -q --depth 1 origin %s{repo.Sha}"
    git "checkout -q FETCH_HEAD"

let ensureClones (repos: Repo list) =
    for repo in repos do
        if not (Directory.Exists(Path.Combine(directory, repo.Name, ".git"))) then
            printfn $"  cloning %s{repo.Name} @ %s{repo.Sha.Substring(0, 10)} ..."
            clone repo

let private skipped =
    set
        [
            "obj"
            "bin"
            "node_modules"
            ".git"
            ".fable"
        ]

let files (repos: Repo list) =
    [|
        for repo in repos do
            let root =
                match repo.Subdirectory with
                | Some subdirectory -> Path.Combine(directory, repo.Name, subdirectory)
                | None -> Path.Combine(directory, repo.Name)

            for path in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories) |> Seq.sort do
                let extension = Path.GetExtension path

                if extension = ".fs" || extension = ".fsx" then
                    let relative = Path.GetRelativePath(directory, path).Replace('\\', '/')

                    if not (relative.Split '/' |> Array.exists skipped.Contains) then
                        {
                            Repo = repo
                            Path = path
                            Relative = relative
                        }
    |]

let read (path: string) = File.ReadAllText path
