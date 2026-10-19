"""Precompute demo PRs for the web app from the cached corpus (and the synthetic fixture)."""

import argparse
import json
import os
import subprocess

CPP_EXT = (".cpp", ".cc", ".cxx", ".c++", ".hpp", ".hh", ".hxx", ".h", ".ipp", ".inl")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENGINE = os.path.join(ROOT, "build/release/foldproof")
OUT = os.path.join(ROOT, "app/public/demos")


def analyse(old_path, new_path, display_path):
    out = subprocess.run([ENGINE, "analyze", "--old", old_path, "--new", new_path, "--path", new_path],
                         capture_output=True, text=True, check=True)
    analysis = json.loads(out.stdout)
    analysis["path"] = display_path
    return analysis


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def from_corpus(key):
    root = os.path.join(ROOT, "eval/cache/prs", key)
    with open(os.path.join(root, "meta.json")) as f:
        meta = json.load(f)
    owner, repo = meta["full_name"].split("/")
    listing = subprocess.run(["gh", "api", "--paginate", f"repos/{meta['full_name']}/pulls/{meta['number']}/files",
                              "--jq", ".[] | [.filename, .status, .changes] | @tsv"],
                             capture_output=True, text=True, check=True).stdout.splitlines()
    files = []
    for line in listing:
        path, status, changes = line.split("\t")
        if path in meta["files"]:
            old_path, new_path = os.path.join(root, "base", path), os.path.join(root, "head", path)
            files.append({"path": path, "old_text": read(old_path), "new_text": read(new_path),
                          "analysis": analyse(old_path, new_path, path)})
            continue
        reason = "not a C++ file" if not path.lower().endswith(CPP_EXT) else f"file was {status}"
        files.append({"path": path, "old_text": "", "new_text": "", "analysis": None, "skipped": reason,
                      "lines": int(changes)})
    return {"owner": owner, "repo": repo, "number": meta["number"], "title": meta["pr_title"],
            "url": f"https://github.com/{meta['full_name']}/pull/{meta['number']}", "agent": meta["agent"],
            "base_sha": meta["base_sha"], "head_sha": meta["head_sha"], "files": files}


def from_fixture():
    root = os.path.join(ROOT, "engine/tests/fixtures/agent_refactor")
    old_path, new_path = os.path.join(root, "old.cpp"), os.path.join(root, "new.cpp")
    return {"owner": "foldproof", "repo": "synthetic", "number": 1,
            "title": "Synthetic example: two moved functions, two renames, a reformat and one real bug",
            "url": "https://github.com/pushpa-kumar/foldproof/tree/main/engine/tests/fixtures/agent_refactor",
            "agent": None, "base_sha": "fixture", "head_sha": "fixture",
            "files": [{"path": "window.cpp", "old_text": read(old_path), "new_text": read(new_path),
                       "analysis": analyse(old_path, new_path, "window.cpp")}]}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("keys", nargs="*", help="corpus keys like owner__repo__number")
    parser.add_argument("--notes", default="{}", help="JSON map key -> one-line note")
    args = parser.parse_args()
    notes = json.loads(args.notes)
    version = subprocess.run([ENGINE, "--version"], capture_output=True, text=True).stdout.strip()
    os.makedirs(OUT, exist_ok=True)
    index = []
    for data in [from_fixture()] + [from_corpus(key) for key in args.keys]:
        data["engine_version"] = version
        key = f"{data['owner']}__{data['repo']}__{data['number']}"
        with open(os.path.join(OUT, f"{key}.json"), "w") as f:
            json.dump(data, f)
        analysed = [f["analysis"] for f in data["files"] if f["analysis"]]
        unanalysed = sum(f.get("lines", 0) for f in data["files"] if not f["analysis"])
        index.append({"owner": data["owner"], "repo": data["repo"], "number": data["number"],
                      "title": data["title"], "agent": data["agent"],
                      "changed": sum(a["changed_lines"] for a in analysed) + unanalysed,
                      "residual": sum(a["residual_lines"] for a in analysed) + unanalysed,
                      "note": notes.get(key, "")})
        print(key, index[-1]["changed"], "->", index[-1]["residual"])
    with open(os.path.join(OUT, "index.json"), "w") as f:
        json.dump(index, f, indent=1)


if __name__ == "__main__":
    main()
