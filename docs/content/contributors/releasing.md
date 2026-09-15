---
title: Releasing
order: 6
---

Releases are automatic. Write Conventional Commits, and merge the release pull request when you want to publish.

## Commit messages

`type(scope): subject`, with an optional scope.

| Type | Version bump |
|---|---|
| `feat`, `perf` | Minor |
| `fix` | Patch |
| `refactor`, `docs`, `test`, `build`, `ci`, `chore` | None |

A `!` after the type, or a `BREAKING CHANGE` footer, bumps the major version.

The scopes in use are `grammar`, `scanner`, `queries`, `highlight` and `signature`. Leave the scope out when a change spans several.

## The release pull request

[EasyBuild.ShipIt](https://github.com/easybuild-org/EasyBuild.ShipIt) runs from `.github/workflows/easybuild-shipit.yml`:

1. After the tests pass on `main`, ShipIt opens or updates the `release/main` pull request. It adds the next `CHANGELOG.md` entry, bumps the version in `tree-sitter.json` and `package.json`, and regenerates `src/` so the parser carries the new version.
2. Merging that pull request pushes a `chore: release` commit. The workflow then builds `tree-sitter-fsharp.wasm` and publishes the GitHub release `vX.Y.Z`, with the Wasm file attached and the changelog entry as notes.

Only the front matter at the top of `CHANGELOG.md` is edited by hand. If a release fails after the pull request is merged, run the workflow manually with the version number.

## The Zed extension

After publishing a release, the release job starts `.github/workflows/zed-extension.yml`, which updates [MangelMaxime/zed-fsharp](https://github.com/MangelMaxime/zed-fsharp) through a pull request:

- Every `[grammars.*]` entry in its `extension.toml` that points at this repository is pinned to the released commit.
- The Zed queries are copied into the `languages/` directory whose `config.toml` uses that grammar.
- The changes go on the `tree-sitter-fsharp/update` branch. The first release opens a pull request from it; later releases update that pull request until it is merged.

To run it for an existing release, start **Zed extension** from the repository's Actions tab with the release tag.

### Setting up the token

The workflow pushes to another repository, which the default `GITHUB_TOKEN` cannot do. It needs a token of its own, stored as the `ZED_FSHARP_TOKEN` secret. This is done once, and again when the token expires.

1. On GitHub, open **Settings**, **Developer settings**, **Personal access tokens**, **Fine-grained tokens**, and click **Generate new token**.
2. Name it, for example `tree-sitter-fsharp zed-extension`, and pick an expiration.
3. **Resource owner**: `MangelMaxime`. **Repository access**: **Only select repositories**, then `zed-fsharp`.
4. Under **Repository permissions**, set **Contents** and **Pull requests** to **Read and write**. **Metadata** turns to read-only by itself.
5. Click **Generate token** and copy it.
6. Store it in this repository, either under **Settings**, **Secrets and variables**, **Actions**, **New repository secret** with the name `ZED_FSHARP_TOKEN`, or from a terminal, which prompts for the value:

```bash
gh secret set ZED_FSHARP_TOKEN -R MangelMaxime/tree-sitter-fsharp
```

:::note
When the token expires, the workflow fails at the push. Generate a new one and set the secret again.
:::

The same command runs from a machine where `gh` is logged in:

```bash
./build.sh zed-extension --dry-run
./build.sh zed-extension --version 0.2.0 --rev <sha>
```

`--dry-run` clones the extension into a temporary directory, applies and commits the change there, and prints what it would push, without pushing.
