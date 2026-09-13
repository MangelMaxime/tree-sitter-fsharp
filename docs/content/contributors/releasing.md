---
title: Releasing
order: 6
---

Releases are cut by [EasyBuild.ShipIt](https://github.com/easybuild-org/EasyBuild.ShipIt) from the Conventional Commits on `main`. The workflow is `.github/workflows/easybuild-shipit.yml`.

1. After the tests pass on `main`, ShipIt opens or updates the `release/main` pull request. It writes the next entry of `CHANGELOG.md`, bumps the version in `tree-sitter.json` and `package.json`, and runs `./build.sh generate --force` so `src/parser.c` carries the version.
2. Merging that pull request pushes a `chore: release X.Y.Z` commit. The workflow builds `tree-sitter-fsharp.wasm` and publishes the GitHub release `vX.Y.Z` with it and the changelog entry as notes.

`feat` and `perf` bump the minor version, `fix` the patch version, a `!` or a `BREAKING CHANGE` footer the major version.

Only the front matter of `CHANGELOG.md` is edited by hand.

## Commit messages

Conventional Commits with the scopes in use: `grammar`, `scanner`, `queries`, `highlight`, `signature`, `build`, `ci`, `docs`.
