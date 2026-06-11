#!/usr/bin/env python3
"""Benchmark the parser against real-world F# projects.

Sweeps every .fs/.fsx file of the repos pinned in scripts/bench-manifest.txt
(23 popular projects + dotnet/fsharp's src/), counts ERROR/MISSING nodes per
file, and diffs the result against the committed baseline
(test/bench/baseline.txt). Any file that parses WORSE than the baseline is a
regression and fails the run — this is the gate every grammar change must
pass.

Usage (from the repo root):

    ./scripts/bench.py                    # sweep + compare against baseline
    ./scripts/bench.py --update-baseline  # accept the current state as baseline
    ./scripts/bench.py --summary          # also print the per-project table

Repos are cloned shallowly at their pinned commits into $FSHARP_BENCH_DIR
(default: ~/.cache/fsharp-grammar-bench) on first run — about 1.5 GB.

Hard-won measurement rules encoded here (do not "simplify" them away):
  * One `tree-sitter parse` PER FILE, executed with cwd = THIS repo — the CLI
    resolves the grammar from cwd, and from any other directory it silently
    serves a different (or no) parser. Batch parses miscount.
  * A sanity probe runs first: a valid snippet must yield a `source_file`
    root (another grammar named `fsharp` may have poisoned the cache) and a
    garbage snippet must yield ERRORs (a stale/broken parser yields none).
"""

import argparse
import concurrent.futures
import re
import subprocess
import sys
import os
import tempfile
from collections import defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MANIFEST = REPO / "scripts" / "bench-manifest.txt"
BASELINE = REPO / "test" / "bench" / "baseline.txt"
BENCH_DIR = Path(os.environ.get("FSHARP_BENCH_DIR", Path.home() / ".cache" / "fsharp-grammar-bench"))
EXCLUDE_DIRS = {"obj", "bin", "node_modules", ".git", ".fable"}
TS = str(REPO / "node_modules" / ".bin" / "tree-sitter")


def read_manifest():
    repos = []
    for line in MANIFEST.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        name, url, sha = parts[0], parts[1], parts[2]
        subdir = parts[3] if len(parts) > 3 else ""
        repos.append((name, url, sha, subdir))
    return repos


def ensure_clones(repos):
    BENCH_DIR.mkdir(parents=True, exist_ok=True)
    for name, url, sha, _ in repos:
        dest = BENCH_DIR / name
        if (dest / ".git").exists():
            continue
        print(f"  cloning {name} @ {sha[:10]} …", flush=True)
        dest.mkdir(parents=True, exist_ok=True)
        run = lambda *cmd: subprocess.run(cmd, cwd=dest, check=True, capture_output=True)
        run("git", "init", "-q")
        run("git", "remote", "add", "origin", url)
        run("git", "fetch", "-q", "--depth", "1", "origin", sha)
        run("git", "checkout", "-q", "FETCH_HEAD")


def list_files(repos):
    files = []
    for name, _, _, subdir in repos:
        root = BENCH_DIR / name / subdir if subdir else BENCH_DIR / name
        for p in sorted(root.rglob("*")):
            if p.suffix not in (".fs", ".fsx"):
                continue
            if any(part in EXCLUDE_DIRS for part in p.parts):
                continue
            files.append((name, p))
    return files


def parse_errors(path):
    try:
        out = subprocess.run(
            [TS, "parse", str(path)],
            cwd=REPO, capture_output=True, text=True, timeout=60,
        ).stdout
    except subprocess.TimeoutExpired:
        return 9999
    return len(re.findall(r"\b(?:ERROR|MISSING)\b \[", out))


def sanity_probe():
    def parse_str(code):
        with tempfile.NamedTemporaryFile("w", suffix=".fsx", delete=False) as f:
            f.write(code)
        out = subprocess.run([TS, "parse", f.name], cwd=REPO,
                             capture_output=True, text=True, timeout=120).stdout
        Path(f.name).unlink()
        return out

    ok = parse_str("let x = 1\n")
    if not ok.startswith("(source_file"):
        sys.exit("sanity probe FAILED: root is not source_file — wrong parser "
                 "is answering (another grammar named 'fsharp' in the cache?). "
                 "Run: rm -rf ~/.cache/tree-sitter and retry.")
    bad = parse_str("let = = ((( garbage\n")
    if "ERROR" not in bad:
        sys.exit("sanity probe FAILED: garbage parsed clean — stale parser. "
                 "Run: rm -rf ~/.cache/tree-sitter and retry.")


def sweep(files, jobs):
    results = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        futs = {ex.submit(parse_errors, path): (name, path) for name, path in files}
        done = 0
        for fut in concurrent.futures.as_completed(futs):
            name, path = futs[fut]
            n = fut.result()
            if n:
                rel = f"{name}/{path.relative_to(BENCH_DIR / name)}"
                results[rel] = n
            done += 1
            if done % 500 == 0:
                print(f"  …{done}/{len(files)} files", flush=True)
    return results


def load_baseline():
    if not BASELINE.exists():
        return None
    out = {}
    for line in BASELINE.read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        n, rel = line.split("\t", 1)
        out[rel] = int(n)
    return out


def save_baseline(results, total_files):
    BASELINE.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Parser benchmark baseline — regenerate with: ./scripts/bench.py --update-baseline",
        f"# {total_files} files swept; {len(results)} with errors; {sum(results.values())} error nodes",
    ]
    lines += [f"{n}\t{rel}" for rel, n in sorted(results.items())]
    BASELINE.write_text("\n".join(lines) + "\n")


def summarize(results, files):
    per_repo = defaultdict(lambda: [0, 0, 0])  # files, failing, nodes
    for name, path in files:
        per_repo[name][0] += 1
    for rel, n in results.items():
        name = rel.split("/", 1)[0]
        per_repo[name][1] += 1
        per_repo[name][2] += n
    print(f"\n{'project':<38}{'files':>7}{'failing':>9}{'clean':>8}{'nodes':>8}")
    for name in sorted(per_repo):
        t, f, n = per_repo[name]
        print(f"{name:<38}{t:>7}{f:>9}{(t - f) / t:>7.1%}{n:>8}")
    total = sum(v[0] for v in per_repo.values())
    failing = sum(v[1] for v in per_repo.values())
    nodes = sum(v[2] for v in per_repo.values())
    print(f"{'TOTAL':<38}{total:>7}{failing:>9}{(total - failing) / total:>7.1%}{nodes:>8}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--update-baseline", action="store_true")
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--jobs", type=int, default=max(4, (os.cpu_count() or 8) - 2))
    args = ap.parse_args()

    repos = read_manifest()
    ensure_clones(repos)
    sanity_probe()
    files = list_files(repos)
    print(f"sweeping {len(files)} files with {args.jobs} workers …", flush=True)
    results = sweep(files, args.jobs)

    print(f"\n{len(files)} files; {len(results)} failing "
          f"({(len(files) - len(results)) / len(files):.1%} clean); "
          f"{sum(results.values())} error nodes")
    if args.summary:
        summarize(results, files)

    if args.update_baseline:
        save_baseline(results, len(files))
        print(f"baseline written: {BASELINE.relative_to(REPO)}")
        return

    base = load_baseline()
    if base is None:
        sys.exit(f"no baseline at {BASELINE.relative_to(REPO)} — "
                 "run with --update-baseline first")

    regressions = {r: (base.get(r, 0), n) for r, n in results.items()
                   if n > base.get(r, 0)}
    fixed = {r: n for r, n in base.items() if r not in results}
    improved = {r: (base[r], results[r]) for r in results
                if r in base and results[r] < base[r]}

    if fixed:
        print(f"\nfixed ({len(fixed)}):")
        for r, n in sorted(fixed.items(), key=lambda kv: -kv[1])[:15]:
            print(f"  -{n:<5} {r}")
    if improved:
        print(f"improved ({len(improved)}):")
        for r, (a, b) in sorted(improved.items(), key=lambda kv: kv[1][1] - kv[1][0])[:15]:
            print(f"  {a}->{b:<5} {r}")
    if regressions:
        print(f"\nREGRESSIONS ({len(regressions)}):")
        for r, (a, b) in sorted(regressions.items(), key=lambda kv: kv[1][0] - kv[1][1])[:30]:
            print(f"  {a}->{b:<5} {r}")
        sys.exit(1)
    print("\nno regressions ✓"
          + ("  (improvements above — run --update-baseline to lock them in)"
             if fixed or improved else ""))


if __name__ == "__main__":
    main()
