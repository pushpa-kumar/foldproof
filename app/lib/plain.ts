import type { Analysis, Hunk } from "./types";
import { splitLines } from "./rows";

export function plainAnalysis(path: string, oldText: string, newText: string, reason: string): Analysis {
  const a = splitLines(oldText);
  const b = splitLines(newText);
  const hunks: Hunk[] = [];
  if (a.length * b.length > 4_000_000) {
    hunks.push({ old_begin: 0, old_end: a.length, new_begin: 0, new_end: b.length, fold: null });
  } else {
    const lcs: Uint32Array[] = Array.from({ length: a.length + 1 }, () => new Uint32Array(b.length + 1));
    for (let i = a.length - 1; i >= 0; i--)
      for (let j = b.length - 1; j >= 0; j--)
        lcs[i][j] = a[i] === b[j] ? lcs[i + 1][j + 1] + 1 : Math.max(lcs[i + 1][j], lcs[i][j + 1]);
    let i = 0;
    let j = 0;
    let open: Hunk | null = null;
    while (i < a.length || j < b.length) {
      if (i < a.length && j < b.length && a[i] === b[j]) {
        if (open) hunks.push(open);
        open = null;
        i++;
        j++;
        continue;
      }
      open ??= { old_begin: i, old_end: i, new_begin: j, new_end: j, fold: null };
      if (j < b.length && (i === a.length || lcs[i][j + 1] >= lcs[i + 1][j])) open.new_end = ++j;
      else open.old_end = ++i;
    }
    if (open) hunks.push(open);
  }
  const changed = hunks.reduce((n, h) => n + (h.old_end - h.old_begin) + (h.new_end - h.new_begin), 0);
  return { path, changed_lines: changed, residual_lines: changed, fallback: reason, hunks, folds: [], refusals: [] };
}
