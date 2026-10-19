import { execFile } from "child_process";
import { promises as fs } from "fs";
import os from "os";
import path from "path";
import { promisify } from "util";
import type { PrData, PrFile } from "@/lib/types";

const run = promisify(execFile);
const CPP = /\.(cpp|cc|cxx|c\+\+|hpp|hh|hxx|h|ipp|inl)$/i;
const ENGINE = process.env.FOLDPROOF_BIN ?? path.join(process.cwd(), "..", "build", "release", "foldproof");
const CACHE = path.join(process.cwd(), ".cache");

async function token(): Promise<string | undefined> {
  if (process.env.GITHUB_TOKEN) return process.env.GITHUB_TOKEN;
  try {
    return (await run("gh", ["auth", "token"])).stdout.trim();
  } catch {
    return undefined;
  }
}

async function github(url: string, auth?: string) {
  const res = await fetch(`https://api.github.com/${url}`, {
    headers: { Accept: "application/vnd.github+json", ...(auth ? { Authorization: `Bearer ${auth}` } : {}) },
  });
  if (!res.ok) throw new Error(`GitHub returned ${res.status} for ${url}`);
  return res.json();
}

async function blob(owner: string, repo: string, sha: string, file: string) {
  const encoded = file.split("/").map(encodeURIComponent).join("/");
  const res = await fetch(`https://raw.githubusercontent.com/${owner}/${repo}/${sha}/${encoded}`);
  if (!res.ok) throw new Error(`Could not download ${file} at ${sha.slice(0, 7)}`);
  return res.text();
}

const NAME = /^(?!\.{1,2}$)[\w.-]+$/;
const LOCAL = new Set(["localhost", "127.0.0.1", "[::1]"]);
let queue: Promise<unknown> = Promise.resolve();

function serialised<T>(job: () => Promise<T>): Promise<T> {
  const next = queue.then(job, job);
  queue = next.catch(() => undefined);
  return next;
}

async function listFiles(owner: string, repo: string, number: number, auth?: string) {
  const files: { filename: string; status: string; changes: number }[] = [];
  for (let page = 1; page <= 30; page++) {
    const batch = await github(`repos/${owner}/${repo}/pulls/${number}/files?per_page=100&page=${page}`, auth);
    files.push(...batch);
    if (batch.length < 100) break;
  }
  return files;
}

export async function GET(request: Request) {
  const url = new URL(request.url);
  const origin = request.headers.get("origin");
  if (!LOCAL.has(url.hostname) || (origin && !LOCAL.has(new URL(origin).hostname)))
    return Response.json({ error: "Live analysis only answers requests from this machine." }, { status: 403 });
  const params = url.searchParams;
  const owner = params.get("owner") ?? "";
  const repo = params.get("repo") ?? "";
  const number = Number(params.get("number"));
  if (!NAME.test(owner) || !NAME.test(repo) || !Number.isInteger(number) || number <= 0)
    return Response.json({ error: "Expected a GitHub PR as owner/repo/number." }, { status: 400 });
  try {
    await fs.access(ENGINE);
  } catch {
    return Response.json(
      { error: "Live analysis needs the native engine, which only runs locally. The hosted site shows the precomputed demos." },
      { status: 501 },
    );
  }
  return serialised(() => analysePr(owner, repo, number));
}

async function analysePr(owner: string, repo: string, number: number) {
  let work: string | null = null;
  try {
    const auth = await token();
    const pr = await github(`repos/${owner}/${repo}/pulls/${number}`, auth);
    const version = (await run(ENGINE, ["--version"])).stdout.trim();
    const cacheFile = path.join(CACHE, `${owner}__${repo}__${pr.base.sha}__${pr.head.sha}__${version.replace(/\W/g, "_")}.json`);
    try {
      return Response.json(JSON.parse(await fs.readFile(cacheFile, "utf8")));
    } catch {}
    const listing = await listFiles(owner, repo, number, auth);
    work = await fs.mkdtemp(path.join(os.tmpdir(), "foldproof-"));
    const files: PrFile[] = [];
    for (const entry of listing) {
      const skip = (reason: string) =>
        files.push({ path: entry.filename, old_text: "", new_text: "", analysis: null, skipped: reason, lines: entry.changes });
      if (!CPP.test(entry.filename)) {
        skip("not a C++ file");
        continue;
      }
      if (entry.status !== "modified") {
        skip(`file was ${entry.status}`);
        continue;
      }
      const [oldText, newText] = await Promise.all([
        blob(owner, repo, pr.base.sha, entry.filename),
        blob(owner, repo, pr.head.sha, entry.filename),
      ]);
      const oldPath = path.resolve(work, "old", entry.filename);
      const newPath = path.resolve(work, "new", entry.filename);
      if (!oldPath.startsWith(work + path.sep) || !newPath.startsWith(work + path.sep)) {
        skip("unsafe file path");
        continue;
      }
      await fs.mkdir(path.dirname(oldPath), { recursive: true });
      await fs.mkdir(path.dirname(newPath), { recursive: true });
      await fs.writeFile(oldPath, oldText);
      await fs.writeFile(newPath, newText);
      const out = await run(ENGINE, ["analyze", "--old", oldPath, "--new", newPath, "--path", newPath], {
        maxBuffer: 64 * 1024 * 1024,
        timeout: 180_000,
      });
      const analysis = JSON.parse(out.stdout);
      analysis.path = entry.filename;
      files.push({ path: entry.filename, old_text: oldText, new_text: newText, analysis });
    }
    const data: PrData = {
      owner, repo, number, title: pr.title, url: pr.html_url, base_sha: pr.base.sha, head_sha: pr.head.sha,
      engine_version: version, files,
    };
    await fs.mkdir(CACHE, { recursive: true });
    await fs.writeFile(cacheFile, JSON.stringify(data));
    return Response.json(data);
  } catch (err) {
    return Response.json({ error: (err as Error).message }, { status: 502 });
  } finally {
    if (work) await fs.rm(work, { recursive: true, force: true });
  }
}
