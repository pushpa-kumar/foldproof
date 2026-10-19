"""Download the AIDev repository and pull_request tables and keep agent PRs on C++ repositories."""

import json
import os
import urllib.request

import pyarrow.parquet as pq

HERE = os.path.dirname(os.path.abspath(__file__))
CACHE = os.path.join(HERE, "cache")
BASE = "https://huggingface.co/datasets/hao-li/AIDev/resolve/refs%2Fconvert%2Fparquet"


def main():
    os.makedirs(CACHE, exist_ok=True)
    for table in ("repository", "pull_request"):
        path = os.path.join(CACHE, f"{table}.parquet")
        if not os.path.exists(path):
            urllib.request.urlretrieve(f"{BASE}/{table}/train/0000.parquet", path)
    repos = {r["id"]: r for r in pq.read_table(os.path.join(CACHE, "repository.parquet")).to_pylist()
             if r["language"] == "C++"}
    columns = ["id", "number", "title", "agent", "state", "merged_at", "repo_id", "repo_url", "html_url"]
    prs = [p for p in pq.read_table(os.path.join(CACHE, "pull_request.parquet"), columns=columns).to_pylist()
           if p["repo_id"] in repos]
    for pr in prs:
        pr["full_name"] = repos[pr["repo_id"]]["full_name"]
        pr["stars"] = repos[pr["repo_id"]]["stars"]
        pr["merged_at"] = str(pr["merged_at"]) if pr["merged_at"] is not None else None
    with open(os.path.join(CACHE, "cpp_prs.json"), "w") as f:
        json.dump(prs, f)
    print(f"{len(prs)} agent PRs on {len(repos)} C++ repositories")


if __name__ == "__main__":
    main()
