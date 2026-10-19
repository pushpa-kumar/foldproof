import { promises as fs } from "fs";
import path from "path";
import type { DemoIndexEntry, PrData } from "./types";

const DIR = path.join(process.cwd(), "public", "demos");

export function demoKey(owner: string, repo: string, number: number | string) {
  return `${owner}__${repo}__${number}`;
}

export async function readDemoIndex(): Promise<DemoIndexEntry[]> {
  try {
    return JSON.parse(await fs.readFile(path.join(DIR, "index.json"), "utf8"));
  } catch {
    return [];
  }
}

export async function readDemo(owner: string, repo: string, number: string): Promise<PrData | null> {
  try {
    return JSON.parse(await fs.readFile(path.join(DIR, `${demoKey(owner, repo, number)}.json`), "utf8"));
  } catch {
    return null;
  }
}
