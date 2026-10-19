"""Differential oracle: every fold Foldproof accepts must leave behaviour unchanged when compiled and run."""

import argparse
import json
import os
import subprocess
import sys
import tempfile
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from generate import generate  # noqa: E402

CXX = os.path.join(os.environ.get("LLVM_PREFIX", "/opt/homebrew/opt/llvm"), "bin/clang++")


def merge(old, new, hunks):
    old_lines = old.splitlines(keepends=True)
    new_lines = new.splitlines(keepends=True)
    out, pos = [], 0
    for hunk in hunks:
        out.extend(old_lines[pos:hunk["old_begin"]])
        if hunk["fold"] is not None:
            out.extend(old_lines[hunk["old_begin"]:hunk["old_end"]])
        else:
            out.extend(new_lines[hunk["new_begin"]:hunk["new_end"]])
        pos = hunk["old_end"]
    out.extend(old_lines[pos:])
    return "".join(out)


def behaviour(source, workdir, tag):
    src = os.path.join(workdir, f"{tag}.cpp")
    exe = os.path.join(workdir, f"{tag}.bin")
    with open(src, "w") as f:
        f.write(source)
    built = subprocess.run([CXX, "-std=c++20", "-w", src, "-o", exe], capture_output=True, text=True)
    if built.returncode != 0:
        return ("compile-error",)
    try:
        ran = subprocess.run([exe], capture_output=True, text=True, timeout=30)
    except subprocess.TimeoutExpired:
        return ("timeout",)
    return ("ran", ran.returncode, ran.stdout)


def run_case(case, binary):
    with tempfile.TemporaryDirectory() as workdir:
        old_path = os.path.join(workdir, "old.cpp")
        new_path = os.path.join(workdir, "new.cpp")
        with open(old_path, "w") as f:
            f.write(case["old"])
        with open(new_path, "w") as f:
            f.write(case["new"])
        out = subprocess.run([binary, "analyze", "--old", old_path, "--new", new_path, "--path",
                              os.path.join(workdir, "t.cpp")], capture_output=True, text=True, timeout=120)
        if out.returncode != 0:
            return {**case, "error": out.stderr[-500:]}
        analysis = json.loads(out.stdout)
        reviewed = merge(case["old"], case["new"], analysis["hunks"])
        result = {"name": case["name"], "rule": case["rule"], "category": case["category"],
                  "changed": analysis["changed_lines"], "residual": analysis["residual_lines"],
                  "folds": len(analysis["folds"]), "kinds": [f["kind"] for f in analysis["folds"]]}
        result["mutation_changes_behaviour"] = behaviour(case["old"], workdir, "o") != behaviour(case["new"], workdir, "n")
        if analysis["folds"]:
            result["false_fold"] = behaviour(reviewed, workdir, "r") != behaviour(case["new"], workdir, "n2")
        else:
            result["false_fold"] = False
        return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--variants", type=int, default=40)
    parser.add_argument("--binary", default=os.path.join(os.path.dirname(__file__), "../../build/release/foldproof"))
    parser.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "results.json"))
    parser.add_argument("--jobs", type=int, default=os.cpu_count())
    args = parser.parse_args()
    cases = generate(args.variants)
    done = [0]

    def tracked(case):
        result = run_case(case, os.path.abspath(args.binary))
        done[0] += 1
        if done[0] % 50 == 0:
            print(f"{done[0]}/{len(cases)} cases", file=sys.stderr, flush=True)
        return result

    with ThreadPoolExecutor(args.jobs) as pool:
        results = list(pool.map(tracked, cases))
    errors = [r for r in results if "error" in r]
    results = [r for r in results if "error" not in r]
    with open(args.out, "w") as f:
        json.dump(results, f, indent=1)

    rows = defaultdict(lambda: defaultdict(int))
    for r in results:
        row = rows[(r["category"], r["rule"])]
        row["cases"] += 1
        row["behaviour_changed"] += r["mutation_changes_behaviour"]
        row["with_folds"] += r["folds"] > 0
        row["false_folds"] += r["false_fold"]
        row["changed"] += r["changed"]
        row["residual"] += r["residual"]
    print(f"{'category':10} {'rule':28} {'cases':>5} {'behav.chg':>9} {'folded':>6} {'false':>5} {'lines->review':>14}")
    total = defaultdict(int)
    for (category, rule), row in sorted(rows.items()):
        print(f"{category:10} {rule:28} {row['cases']:5} {row['behaviour_changed']:9} {row['with_folds']:6} "
              f"{row['false_folds']:5} {row['changed']:6}->{row['residual']:<6}")
        for key, value in row.items():
            total[key] += value
    print(f"{'TOTAL':39} {total['cases']:5} {total['behaviour_changed']:9} {total['with_folds']:6} "
          f"{total['false_folds']:5} {total['changed']:6}->{total['residual']:<6}")
    if errors:
        print(f"{len(errors)} cases errored, first: {errors[0]['name']}: {errors[0]['error']}")
    return 1 if total["false_folds"] or errors else 0


if __name__ == "__main__":
    sys.exit(main())
