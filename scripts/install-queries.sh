#!/usr/bin/env bash
# Install tree-sitter-fsharp-helix's query files into Helix's runtime
# queries directory.
#
# Helix's `hx --grammar fetch` / `hx --grammar build` handles the parser
# binary, but it doesn't install queries (highlights.scm, indents.scm,
# …). This script fetches them straight from GitHub and drops them into
# `~/.config/helix/runtime/queries/fsharp/`.
#
# Usage:
#   ./install-queries.sh                # default branch `main`
#   ./install-queries.sh some-branch    # specific branch
#   ./install-queries.sh <full-sha>     # pin to a commit
#
# Or one-shot from anywhere:
#   curl -fsSL https://raw.githubusercontent.com/MangelMaxime/tree-sitter-fsharp/main/scripts/install-queries.sh | bash
#   curl -fsSL https://raw.githubusercontent.com/MangelMaxime/tree-sitter-fsharp/main/scripts/install-queries.sh | bash -s -- some-branch
#
# Use a different repo (e.g. a fork) by setting TS_FSHARP_REPO:
#   TS_FSHARP_REPO=YourFork/your-repo ./install-queries.sh some-branch

set -euo pipefail

REPO="${TS_FSHARP_REPO:-MangelMaxime/tree-sitter-fsharp}"
REF="${1:-main}"
DEST="${HELIX_RUNTIME:-$HOME/.config/helix/runtime}/queries/fsharp"

# Every query file we ship — listed explicitly so we can also create
# empty placeholders for any that aren't in the repo. Without the empty
# placeholders, Helix would fall back to its built-in queries (which
# reference node types from the upstream Ionide grammar that don't exist
# here), corrupting highlighting in subtle ways.
QUERIES=(highlights indents injections locals rainbows tags textobjects)

if ! command -v curl >/dev/null 2>&1; then
    echo "error: curl is required" >&2
    exit 1
fi

mkdir -p "$DEST"

echo "Installing queries from $REPO@$REF → $DEST"

for q in "${QUERIES[@]}"; do
    url="https://raw.githubusercontent.com/$REPO/$REF/queries/${q}.scm"
    out="$DEST/${q}.scm"
    # Use -w to grab the HTTP status separately from the body so we can
    # tell "404 -> create empty placeholder" apart from a transport error.
    http=$(curl -fsSL -o "$out.tmp" -w "%{http_code}" "$url" 2>/dev/null || true)
    if [ "$http" = "200" ]; then
        mv "$out.tmp" "$out"
        size=$(wc -c < "$out")
        echo "  ${q}.scm  (${size} bytes)"
    else
        rm -f "$out.tmp"
        # File doesn't exist in the repo at this ref — write an empty
        # placeholder so Helix doesn't fall back to built-ins.
        : > "$out"
        echo "  ${q}.scm  (missing in repo; wrote empty placeholder)"
    fi
done

echo ""
echo "Done. Reload any open .fs / .fsx buffers in Helix (:reload)."
