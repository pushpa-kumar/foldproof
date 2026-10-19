import type { Analysis, Fold, Hunk } from "./types";

export interface SeamPart {
  fold: number;
  hunk: number;
  lead: boolean;
  blank: boolean;
}

export type Row =
  | { type: "context"; oldNo: number; newNo: number; text: string }
  | { type: "del"; oldNo: number; text: string; hunk: number }
  | { type: "add"; newNo: number; text: string; hunk: number }
  | { type: "gap"; count: number }
  | { type: "seam"; parts: SeamPart[]; hidden: Row[] }
  | { type: "residual-start"; hunk: number; refusal: string | null };

const CONTEXT = 3;

export function splitLines(text: string): string[] {
  const lines = text.split("\n");
  if (lines.length && lines[lines.length - 1] === "") lines.pop();
  return lines;
}

function hunkRows(hunk: Hunk, index: number, oldLines: string[], newLines: string[]): Row[] {
  const rows: Row[] = [];
  for (let i = hunk.old_begin; i < hunk.old_end; i++) rows.push({ type: "del", oldNo: i + 1, text: oldLines[i], hunk: index });
  for (let i = hunk.new_begin; i < hunk.new_end; i++) rows.push({ type: "add", newNo: i + 1, text: newLines[i], hunk: index });
  return rows;
}

export function consistent(analysis: Analysis, oldText: string, newText: string): boolean {
  const oldCount = splitLines(oldText).length;
  const newCount = splitLines(newText).length;
  let oldPos = 0;
  let newPos = 0;
  let residual = 0;
  for (const hunk of analysis.hunks) {
    if (hunk.old_begin < oldPos || hunk.new_begin < newPos) return false;
    if (hunk.old_begin - oldPos !== hunk.new_begin - newPos) return false;
    if (hunk.old_end < hunk.old_begin || hunk.new_end < hunk.new_begin) return false;
    if (hunk.fold === null) residual += hunk.old_end - hunk.old_begin + (hunk.new_end - hunk.new_begin);
    oldPos = hunk.old_end;
    newPos = hunk.new_end;
  }
  return oldCount - oldPos === newCount - newPos && oldPos <= oldCount && residual === analysis.residual_lines;
}

export function buildRows(analysis: Analysis, oldText: string, newText: string): Row[] {
  const oldLines = splitLines(oldText);
  const newLines = splitLines(newText);
  const refusalOf = new Map<number, string>();
  for (const refusal of analysis.refusals) for (const h of refusal.hunks) refusalOf.set(h, refusal.reason);
  const leadOf = new Map<number, number>();
  analysis.folds.forEach((fold: Fold, id) => leadOf.set(id, Math.min(...fold.hunks)));

  const rows: Row[] = [];
  let oldPos = 0;
  let newPos = 0;
  const emitContext = (oldEnd: number, isFirst: boolean, isLast: boolean) => {
    const count = oldEnd - oldPos;
    const head = isFirst ? 0 : Math.min(CONTEXT, count);
    const tail = isLast ? 0 : Math.min(CONTEXT, count - head);
    for (let i = 0; i < head; i++) rows.push({ type: "context", oldNo: oldPos + i + 1, newNo: newPos + i + 1, text: oldLines[oldPos + i] });
    if (count - head - tail > 0) rows.push({ type: "gap", count: count - head - tail });
    for (let i = count - tail; i < count; i++) rows.push({ type: "context", oldNo: oldPos + i + 1, newNo: newPos + i + 1, text: oldLines[oldPos + i] });
    newPos += count;
    oldPos = oldEnd;
  };

  analysis.hunks.forEach((hunk, index) => {
    emitContext(hunk.old_begin, index === 0, false);
    if (hunk.fold !== null) {
      const hidden = hunkRows(hunk, index, oldLines, newLines);
      const blank = hidden.every((row) => (row.type === "add" || row.type === "del") && row.text.trim() === "");
      rows.push({ type: "seam", parts: [{ fold: hunk.fold, hunk: index, lead: leadOf.get(hunk.fold) === index, blank }], hidden });
    } else {
      rows.push({ type: "residual-start", hunk: index, refusal: refusalOf.get(index) ?? null });
      rows.push(...hunkRows(hunk, index, oldLines, newLines));
    }
    oldPos = hunk.old_end;
    newPos = hunk.new_end;
  });
  emitContext(oldLines.length, false, true);
  return mergeSeams(rows);
}

function mergeSeams(rows: Row[]): Row[] {
  const out: Row[] = [];
  for (const row of rows) {
    const prev = out[out.length - 1];
    if (row.type === "seam" && prev && prev.type === "seam") {
      prev.hidden.push(...row.hidden);
      prev.parts.push(...row.parts);
      continue;
    }
    out.push(row.type === "seam" ? { ...row, parts: [...row.parts], hidden: [...row.hidden] } : row);
  }
  return out;
}

export function describePart(analysis: Analysis, part: SeamPart): string | null {
  const fold = analysis.folds[part.fold];
  if (fold.kind === "format") return part.blank ? null : "Formatting only";
  if (fold.kind !== "move") return part.lead ? describeFold(fold) : `${describeFold(fold)} (continued)`;
  const here = analysis.hunks[part.hunk];
  if (fold.hunks.length !== 2) return describeFold(fold);
  const otherIndex = fold.hunks.find((h) => h !== part.hunk) ?? part.hunk;
  const other = analysis.hunks[otherIndex];
  const names = fold.moved.join(", ");
  const renamed = fold.renames.length ? `, renaming ${fold.renames.map((r) => `${r.from} to ${r.to}`).join(" and ")}` : "";
  if (here.new_begin === here.new_end) return `${names} moved from here to new line ${other.new_begin + 1}${renamed}`;
  return `${names} moved here from old line ${other.old_begin + 1}${renamed}`;
}

export function describeFold(fold: Fold): string {
  if (fold.kind === "move") {
    const names = fold.moved.join(", ");
    const renamed = fold.renames.length ? `, with ${fold.renames.map((r) => `${r.from} renamed to ${r.to}`).join(", ")}` : "";
    return `Moved ${names}${renamed}`;
  }
  if (fold.kind === "rename") {
    return fold.renames.map((r) => `Renamed ${r.from} to ${r.to} in ${r.function}, ${r.sites} ${r.sites === 1 ? "site" : "sites"}`).join("; ");
  }
  return "Formatting only";
}

export function foldEvidence(fold: Fold): string[] {
  const common = "libclang parsed the reviewed file and the new file; this fold was checked together with every other fold in the file.";
  if (fold.kind === "format") {
    return [
      "Token streams are identical, comments match after whitespace normalisation.",
      "Whitespace is unchanged inside preprocessor directives, macro arguments that could be stringified, and #if-disabled code.",
      common,
    ];
  }
  if (fold.kind === "rename") {
    return [
      "Every use of each renamed variable resolves to the same local declaration, and no other name changed meaning.",
      "The function has no compiler errors, no directives, and uses no macro that stringizes, pastes, or mentions the old or new name.",
      common,
    ];
  }
  return [
    "Every reference in the file resolves to the same declaration after the move, including overloaded operators.",
    "The moved function crosses no #pragma or #define region and does not observe __LINE__ or __COUNTER__.",
    common,
  ];
}
