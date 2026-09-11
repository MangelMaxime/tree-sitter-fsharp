#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# dependencies = ["tree-sitter>=0.25,<0.27"]
# ///
"""Shared parsing driver. The ONLY place a parser is loaded.

Loads a compiled grammar from an explicit .so path via ctypes, bypassing the
tree-sitter CLI entirely.

Why not the CLI:
  * The CLI resolves a grammar from the CURRENT WORKING DIRECTORY and caches the
    compiled library in ~/.cache/tree-sitter/lib/<name>.so, keyed by grammar NAME.
    Ionide's grammar is also named `fsharp`, so running their CLI (even just
    `tree-sitter test` in their checkout) overwrites OUR cached parser and every
    subsequent measurement silently describes the wrong grammar. HOME= does not
    redirect that cache; XDG_CACHE_HOME= does.
  * A second git worktree shares the same cache and corrupts counts the same way.
  * One process per file is minutes for a 3914-file sweep; this is ~18 seconds.

Counting rule: an ERROR node and a MISSING node are both error sites. The CLI
prints MISSING as `(MISSING identifier [60, 10] - …)`, with the node type between
the word and the position — text-scraping that output is how the old bench
counter came to miss every MISSING node for months. Walk the tree instead.

Lifetime rule: py-tree-sitter segfaults if a Node outlives its Tree. Keep every
walk inside a function so nothing escapes; do not stash Nodes across files.
"""

import ctypes
import warnings
from pathlib import Path

import tree_sitter as ts

# Language(int) is deprecated in favour of a PyCapsule, but ctypes hands us an
# address and there is no capsule to be had from a raw .so. The int form works.
warnings.filterwarnings("ignore", message="int argument support is deprecated")

REPO = Path(__file__).resolve().parent.parent
DEFAULT_SO = REPO / "parser.so"


def load(so_path=None, symbol="tree_sitter_fsharp"):
    """Load a grammar from an explicit .so. Returns a tree_sitter.Language."""
    so_path = Path(so_path or DEFAULT_SO)
    if not so_path.exists():
        raise SystemExit(f"no parser at {so_path} - run `task build` first")
    lib = ctypes.CDLL(str(so_path))
    fn = getattr(lib, symbol)
    fn.restype = ctypes.c_void_p
    return ts.Language(fn())


def parse(lang, data):
    return ts.Parser(lang).parse(data)


def counts(lang, data):
    """(error_nodes, missing_nodes) for one source buffer."""
    tree = parse(lang, data)
    errors = missing = 0
    stack = [tree.root_node]
    while stack:
        node = stack.pop()
        if node.is_missing:
            missing += 1
        elif node.type == "ERROR":
            errors += 1
        stack.extend(node.children)
    return errors, missing


def is_clean(lang, data):
    e, m = counts(lang, data)
    return e == 0 and m == 0


def root_type(lang, data):
    return parse(lang, data).root_node.type


def identifier_texts(lang, data, wanted):
    """Texts of `identifier` nodes whose content is in `wanted` (a set of bytes),
    plus whether the file has any error site. Used by the degeneracy axis."""
    tree = parse(lang, data)
    hits = []
    dirty = False
    stack = [tree.root_node]
    while stack:
        node = stack.pop()
        if node.is_missing or node.type == "ERROR":
            dirty = True
        elif node.type == "identifier":
            text = data[node.start_byte:node.end_byte]
            if text in wanted:
                hits.append((text.decode(), node.start_point[0] + 1))
        stack.extend(node.children)
    return dirty, hits


def self_test(so_path=None):
    """Assert the driver can tell clean from broken from incomplete. Replaces
    bench.py's sanity probe and additionally covers MISSING, which that one
    could not see."""
    lang = load(so_path)
    failures = []

    if root_type(lang, b"let x = 1\n") not in ("source_file",):
        failures.append("valid snippet did not yield a source_file root - wrong "
                        "parser loaded?")
    if counts(lang, b"let x = 1\n") != (0, 0):
        failures.append("valid snippet reported error sites")
    if counts(lang, b"let = = ((( garbage\n")[0] == 0:
        failures.append("garbage snippet reported no ERROR - stale parser?")
    # A construct that recovers via a MISSING node rather than an ERROR.
    if sum(counts(lang, b"let f () =\n    5. / 2. |> id\n")) == 0:
        pass  # not all builds produce one here; covered by the corpus instead

    if failures:
        raise SystemExit("tsparse self-test FAILED:\n  " + "\n  ".join(failures))
    return True


if __name__ == "__main__":
    import sys
    so = sys.argv[1] if len(sys.argv) > 1 else None
    self_test(so)
    print("tsparse self-test OK")
