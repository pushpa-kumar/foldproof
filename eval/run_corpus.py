"""Measure lines-to-review on real C++ agent PRs: raw diff, git -w, git -w --color-moved, difftastic, Foldproof."""

import argparse
import glob
import json
import os
import re
import subprocess
import time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ANSI_LINE = re.compile(r"^\x1b\[([0-9;]*)m([+-])")


def git_counts(old_path, new_path):
    def numstat(*flags):
        out = subprocess.run(["git", "diff", "--no-index", "--numstat", *flags, old_path, new_path],
                             capture_output=True, text=True).stdout.split()
        return int(out[0]) + int(out[1]) if len(out) >= 2 and out[0].isdigit() else 0

    colored = subprocess.run(["git", "diff", "--no-index", "-w", "--color=always", "--color-moved=plain",
                              "--color-moved-ws=ignore-all-space", old_path, new_path],
                             capture_output=True, text=True, errors="replace").stdout
    unmoved = 0
    for line in colored.splitlines():
        match = ANSI_LINE.match(line)
        if match and match.group(1) in ("31", "32") and not line.startswith(("\x1b[1m---", "\x1b[1m+++")):
            unmoved += 1
    return numstat(), numstat("-w"), unmoved


def difft_count(old_path, new_path):
    out = subprocess.run(["difft", "--display", "json", old_path, new_path], capture_output=True, text=True,
                         env={**os.environ, "DFT_UNSTABLE": "yes"}, timeout=120)
    try:
        data = json.loads(out.stdout)
    except json.JSONDecodeError:
        return None
    lhs, rhs = set(), set()
    for chunk in data.get("chunks", []):
        for entry in chunk:
            if entry.get("lhs") and entry["lhs"].get("changes"):
                lhs.add(entry["lhs"]["line_number"])
            if entry.get("rhs") and entry["rhs"].get("changes"):
                rhs.add(entry["rhs"]["line_number"])
    return len(lhs) + len(rhs)


def analyze_file(binary, pr_dir, path):
    old_path = os.path.join(pr_dir, "base", path)
    new_path = os.path.join(pr_dir, "head", path)
    started = time.time()
    out = subprocess.run([binary, "analyze", "--old", old_path, "--new", new_path, "--path", new_path],
                         capture_output=True, text=True, timeout=300)
    elapsed = time.time() - started
    if out.returncode != 0:
        return None
    analysis = json.loads(out.stdout)
    raw, ws, unmoved = git_counts(old_path, new_path)
    return {"pr": os.path.basename(pr_dir), "path": path, "seconds": round(elapsed, 2),
            "foldproof_changed": analysis["changed_lines"], "foldproof_residual": analysis["residual_lines"],
            "git_raw": raw, "git_w": ws, "git_w_moved": unmoved, "difftastic": difft_count(old_path, new_path),
            "fallback": analysis["fallback"], "folds": [f["kind"] for f in analysis["folds"]],
            "refusals": [r["reason"] for r in analysis["refusals"]]}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default=os.path.join(HERE, "../build/release/foldproof"))
    parser.add_argument("--jobs", type=int, default=os.cpu_count())
    args = parser.parse_args()
    jobs = []
    for meta_path in sorted(glob.glob(os.path.join(HERE, "cache/prs/*/meta.json"))):
        with open(meta_path) as f:
            meta = json.load(f)
        for path in meta["files"]:
            jobs.append((os.path.dirname(meta_path), path, meta["agent"]))
    binary = os.path.abspath(args.binary)
    with ThreadPoolExecutor(args.jobs) as pool:
        rows = list(pool.map(lambda job: (analyze_file(binary, job[0], job[1]), job[2]), jobs))
    results = []
    for row, agent in rows:
        if row:
            row["agent"] = agent
            results.append(row)
    with open(os.path.join(HERE, "corpus_results.json"), "w") as f:
        json.dump(results, f, indent=1)
    write_report(results)


def write_report(results):
    analysed = [r for r in results if not r["fallback"]]
    total = lambda key: sum(r[key] or 0 for r in analysed)
    raw = total("git_raw")
    lines = ["# Corpus results", "",
             f"{len({r['pr'] for r in results})} agent-authored PRs, {len(results)} modified C++ files "
             f"({len(analysed)} analysed, {len(results) - len(analysed)} fell back to a plain diff).", "",
             "| method | lines to review | vs raw |", "|---|---:|---:|"]
    for label, key in (("raw diff (git)", "git_raw"), ("git diff -w", "git_w"),
                       ("git diff -w --color-moved", "git_w_moved"), ("difftastic", "difftastic"),
                       ("Foldproof", "foldproof_residual")):
        value = total(key)
        lines.append(f"| {label} | {value} | {100 * value / raw:.1f}% |" if raw else f"| {label} | {value} | |")
    folded = [r for r in analysed if r["folds"]]
    kinds = Counter(kind for r in analysed for kind in r["folds"])
    reasons = Counter(re.sub(r"'[^']*'", "'…'", reason) for r in analysed for reason in r["refusals"])
    lines += ["", f"Files with at least one verified fold: {len(folded)} of {len(analysed)}.",
              f"Folds by kind: {dict(kinds)}.", "",
              "Most common refusal reasons:", ""]
    lines += [f"- {count} × {reason}" for reason, count in reasons.most_common(8)]
    seconds = sorted(r["seconds"] for r in results)
    if seconds:
        lines += ["", f"Analysis time per file: median {seconds[len(seconds) // 2]} s, "
                      f"p90 {seconds[int(len(seconds) * 0.9)]} s, max {seconds[-1]} s (release build, no include paths)."]
    with open(os.path.join(HERE, "results.md"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
