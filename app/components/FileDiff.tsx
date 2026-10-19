"use client";

import { useEffect, useMemo, useState } from "react";
import { buildRows, consistent, describePart, foldEvidence, type Row } from "@/lib/rows";
import { plainAnalysis } from "@/lib/plain";
import type { PrFile } from "@/lib/types";

function Line({ row }: { row: Row }) {
  if (row.type === "context")
    return (
      <div className="line ctx">
        <span className="no">{row.oldNo}</span>
        <span className="no">{row.newNo}</span>
        <code>{" " + row.text}</code>
      </div>
    );
  if (row.type === "del")
    return (
      <div className="line del">
        <span className="no">{row.oldNo}</span>
        <span className="no" />
        <code>{"-" + row.text}</code>
      </div>
    );
  if (row.type === "add")
    return (
      <div className="line add">
        <span className="no" />
        <span className="no">{row.newNo}</span>
        <code>{"+" + row.text}</code>
      </div>
    );
  return null;
}

export default function FileDiff({ file, openAll }: { file: PrFile; openAll: boolean }) {
  const analysis = useMemo(() => {
    if (file.analysis && consistent(file.analysis, file.old_text, file.new_text)) return file.analysis;
    if (file.analysis) return plainAnalysis(file.path, file.old_text, file.new_text, "the analysis did not match the file contents");
    return null;
  }, [file]);
  const rows = useMemo(() => (analysis ? buildRows(analysis, file.old_text, file.new_text) : []), [analysis, file]);
  const [open, setOpen] = useState<Set<number>>(new Set());
  useEffect(() => setOpen(new Set()), [openAll]);
  const residualCount = rows.filter((row) => row.type === "residual-start").length;
  const toggle = (key: number) =>
    setOpen((current) => {
      const next = new Set(current);
      if (next.has(key)) next.delete(key);
      else next.add(key);
      return next;
    });

  if (!analysis) {
    return (
      <section className="file">
        <header className="file-head">
          <h3>{file.path}</h3>
        </header>
        <p className="banner">
          Not analysed and not shown here: {file.skipped ?? "not a modified C++ source file"}
          {file.lines ? `. Its ${file.lines} changed lines count towards what you need to read.` : "."}
        </p>
      </section>
    );
  }

  return (
    <section className="file" id={`file-${file.path}`}>
      <header className="file-head">
        <h3>{file.path}</h3>
        <span className="file-count">
          {analysis.residual_lines} of {analysis.changed_lines} lines to read
        </span>
      </header>
      {analysis.fallback && <p className="banner">Plain diff: {analysis.fallback}.</p>}
      <div className="diff">
        {rows.map((row, i) => {
          if (row.type === "gap")
            return (
              <div key={i} className="gap">
                {row.count} unchanged {row.count === 1 ? "line" : "lines"}
              </div>
            );
          if (row.type === "residual-start") {
            const order = rows.slice(0, i).filter((r) => r.type === "residual-start").length + 1;
            const hunk = analysis.hunks[row.hunk];
            return (
              <div
                key={i}
                className="residual-anchor"
                data-residual
                tabIndex={-1}
                aria-label={`${file.path}: change ${order} of ${residualCount}, new lines ${hunk.new_begin + 1} to ${Math.max(hunk.new_end, hunk.new_begin + 1)}`}
              >
                <span className="change-label">
                  Change {order} of {residualCount}
                </span>
                {row.refusal && (
                  <p className="refusal">
                    <strong>Not folded.</strong> {row.refusal}
                  </p>
                )}
              </div>
            );
          }
          if (row.type === "seam") {
            const expanded = openAll || open.has(i);
            const lines = row.hidden.length;
            const labels = row.parts.map((part) => describePart(analysis, part)).filter((label): label is string => !!label);
            const unique = Array.from(new Set(labels));
            const kinds = Array.from(new Set(row.parts.map((part) => analysis.folds[part.fold].kind)));
            return (
              <div key={i} className="seam">
                <button className="seam-bar" aria-expanded={expanded} onClick={() => toggle(i)}>
                  <span className="seam-text">{unique.length ? unique.join("; ") : "Blank lines only"}</span>
                  <span className="seam-meta">
                    {lines} {lines === 1 ? "line" : "lines"} verified equivalent
                  </span>
                </button>
                {expanded && (
                  <div className="seam-body">
                    <ul className="evidence">
                      {kinds.flatMap((kind) =>
                        foldEvidence(analysis.folds[row.parts.find((p) => analysis.folds[p.fold].kind === kind)!.fold]),
                      ).filter((item, k, all) => all.indexOf(item) === k).map((item) => (
                        <li key={item}>{item}</li>
                      ))}
                    </ul>
                    {row.hidden.map((hidden, j) => (
                      <Line key={j} row={hidden} />
                    ))}
                  </div>
                )}
              </div>
            );
          }
          return <Line key={i} row={row} />;
        })}
      </div>
    </section>
  );
}
