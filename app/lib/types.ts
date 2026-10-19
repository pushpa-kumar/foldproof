export type FoldKind = "format" | "rename" | "move";

export interface Hunk {
  old_begin: number;
  old_end: number;
  new_begin: number;
  new_end: number;
  fold: number | null;
}

export interface RenameCert {
  function: string;
  from: string;
  to: string;
  sites: number;
}

export interface Fold {
  kind: FoldKind;
  hunks: number[];
  renames: RenameCert[];
  moved: string[];
}

export interface Refusal {
  proposed: FoldKind;
  hunks: number[];
  reason: string;
  new_line: number;
}

export interface Analysis {
  path: string;
  changed_lines: number;
  residual_lines: number;
  fallback: string | null;
  hunks: Hunk[];
  folds: Fold[];
  refusals: Refusal[];
}

export interface PrFile {
  path: string;
  old_text: string;
  new_text: string;
  analysis: Analysis | null;
  skipped?: string;
  lines?: number;
}

export interface PrData {
  owner: string;
  repo: string;
  number: number;
  title: string;
  url: string;
  agent?: string;
  base_sha: string;
  head_sha: string;
  engine_version: string;
  files: PrFile[];
}

export interface DemoIndexEntry {
  owner: string;
  repo: string;
  number: number;
  title: string;
  agent?: string;
  changed: number;
  residual: number;
  note: string;
}
