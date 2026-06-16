#!/usr/bin/env bash
set -euo pipefail

# Build the grammar and open it in an isolated, repo-local Neovim config
# (nvim/init.lua) for quick eyeballing. Loads no plugins or user config: the
# parser and queries come straight from this repo.
#
#   ./dev-nvim.sh                      # opens examples/references.fsx
#   ./dev-nvim.sh path/to/file.fsx     # opens a specific file

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# --- Compile ---
if [ "$REPO_DIR/src/parser.c" -nt "$REPO_DIR/grammar.js" ] && \
   [ "$REPO_DIR/src/parser.c" -nt "$REPO_DIR/src/scanner.c" ]; then
    echo "Skipping parser generation (grammar unchanged)..."
else
    echo "Generating parser..."
    tree-sitter generate "$REPO_DIR/grammar.js"
fi

echo "Building parser.so..."
tree-sitter build --output "$REPO_DIR/parser.so"

# --- Launch ---
# -u replaces the init file, so ~/.config/nvim is bypassed entirely.
exec nvim -u "$REPO_DIR/nvim/init.lua" "${@:-$REPO_DIR/examples/references.fsx}"
