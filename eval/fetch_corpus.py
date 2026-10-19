"""Sample C++ agent-authored PRs from AIDev and cache base/head blobs of their changed C++ files."""

import argparse
import json
import os
import random
import subprocess
import urllib.request
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
CACHE = os.path.join(HERE, "cache")
CPP_EXT = (".cpp", ".cc", ".cxx", ".c++", ".hpp", ".hh", ".hxx", ".h", ".ipp", ".inl")


def gh(path):
    out = subprocess.run(["gh", "api", "--paginate", path], capture_output=True, text=True)
    if out.returncode != 0:
        raise RuntimeError(out.stderr.strip()[:200])
    text = out.stdout.replace("][", ",")
    return json.loads(text)


def raw(full_name, sha, path):
    url = f"https://raw.githubusercontent.com/{full_name}/{sha}/{urllib.request.quote(path)}"
    with urllib.request.urlopen(url, timeout=30) as resp:
        return resp.read()


def fetch(pr, max_files):
    key = f"{pr['full_name'].replace('/', '__')}__{pr['number']}"
    root = os.path.join(CACHE, "prs", key)
    if os.path.exists(os.path.join(root, "meta.json")):
        return key, "cached"
    try:
        info = gh(f"repos/{pr['full_name']}/pulls/{pr['number']}")
        files = gh(f"repos/{pr['full_name']}/pulls/{pr['number']}/files")
    except RuntimeError as err:
        return key, f"skip: {err}"
    base, head = info["base"]["sha"], info["head"]["sha"]
    kept = []
    for entry in files:
        if not entry["filename"].lower().endswith(CPP_EXT) or entry["status"] != "modified":
            continue
        if len(kept) >= max_files:
            break
        try:
            old, new = raw(pr["full_name"], base, entry["filename"]), raw(pr["full_name"], head, entry["filename"])
        except Exception as err:
            continue
        for side, blob in (("base", old), ("head", new)):
            path = os.path.join(root, side, entry["filename"])
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as f:
                f.write(blob)
        kept.append(entry["filename"])
    os.makedirs(root, exist_ok=True)
    meta = {**pr, "base_sha": base, "head_sha": head, "files": kept, "pr_title": info["title"],
            "additions": info["additions"], "deletions": info["deletions"]}
    with open(os.path.join(root, "meta.json"), "w") as f:
        json.dump(meta, f, indent=1)
    return key, f"{len(kept)} files"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sample", type=int, default=150)
    parser.add_argument("--max-files", type=int, default=25)
    parser.add_argument("--seed", type=int, default=2027)
    args = parser.parse_args()
    with open(os.path.join(CACHE, "cpp_prs.json")) as f:
        prs = json.load(f)
    by_agent = defaultdict(list)
    for pr in prs:
        by_agent[pr["agent"]].append(pr)
    rng = random.Random(args.seed)
    for group in by_agent.values():
        rng.shuffle(group)
    sample, quota = [], max(1, args.sample // len(by_agent))
    for agent, group in sorted(by_agent.items()):
        sample.extend(group[:quota])
    rest = [pr for group in by_agent.values() for pr in group[quota:]]
    rng.shuffle(rest)
    sample.extend(rest[: max(0, args.sample - len(sample))])
    with ThreadPoolExecutor(8) as pool:
        for key, status in pool.map(lambda pr: fetch(pr, args.max_files), sample):
            print(f"{key}: {status}")


if __name__ == "__main__":
    main()
