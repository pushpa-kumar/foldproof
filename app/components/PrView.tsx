"use client";

import Link from "next/link";
import { useEffect, useState } from "react";
import FileDiff from "./FileDiff";
import type { PrData } from "@/lib/types";

export default function PrView({ data }: { data: PrData }) {
  const [openAll, setOpenAll] = useState(false);
  const [help, setHelp] = useState(false);
  const analysed = data.files.filter((f) => f.analysis);
  const skipped = data.files.filter((f) => !f.analysis);
  const skippedLines = skipped.reduce((n, f) => n + (f.lines ?? 0), 0);
  const changed = analysed.reduce((n, f) => n + (f.analysis?.changed_lines ?? 0), 0) + skippedLines;
  const residual = analysed.reduce((n, f) => n + (f.analysis?.residual_lines ?? 0), 0) + skippedLines;
  const folds = analysed.reduce((n, f) => n + (f.analysis?.folds.length ?? 0), 0);
  const share = changed ? residual / changed : 1;

  useEffect(() => {
    let cursor = -1;
    const onKey = (event: KeyboardEvent) => {
      const target = event.target as HTMLElement;
      if (target.closest("input, textarea") || event.metaKey || event.ctrlKey || event.altKey) return;
      const anchors = Array.from(document.querySelectorAll<HTMLElement>("[data-residual]"));
      if (event.key === "j" || event.key === "k") {
        if (!anchors.length) return;
        cursor = event.key === "j" ? Math.min(anchors.length - 1, cursor + 1) : Math.max(0, cursor - 1);
        anchors.forEach((a) => a.classList.remove("current"));
        anchors[cursor].classList.add("current");
        const reduce = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
        anchors[cursor].scrollIntoView({ block: "center", behavior: reduce ? "auto" : "smooth" });
        anchors[cursor].focus({ preventScroll: true });
      } else if (event.key === "f") {
        setOpenAll((v) => !v);
      } else if (event.key === "?") {
        setHelp((v) => !v);
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, []);

  return (
    <main className="pr">
      <header className="pr-head">
        <p className="crumbs">
          <Link href="/">Foldproof</Link> / {data.owner}/{data.repo} #{data.number}
          {data.agent ? ` by ${data.agent.replace("_", " ")}` : ""}
        </p>
        <h1 className="pr-title">
          <a href={data.url}>{data.title}</a>
        </h1>
        <p className="headline">
          <span className="big">{residual}</span> of {changed} changed lines need your eyes.
        </p>
        <div className="ratio" aria-hidden="true">
          <span className="ratio-read" style={{ width: `${Math.max(share * 100, residual ? 0.6 : 0)}%` }} />
          <span className="ratio-folded" />
        </div>
        <p className="sub">
          {changed - residual} lines sit in {folds} {folds === 1 ? "fold" : "folds"} that clang verified as
          equivalent
          {skipped.length
            ? `; ${skipped.length} ${skipped.length === 1 ? "file was" : "files were"} not analysed and ${skipped.length === 1 ? "counts" : "count"} in full`
            : ""}
          .{" "}
          <Link href="/assumptions">What &ldquo;verified&rdquo; assumes</Link>. Press <kbd>j</kbd>/<kbd>k</kbd> to step
          through what is left, <kbd>f</kbd> to open every fold, <kbd>?</kbd> for help.
        </p>
        {help && (
          <div className="help" role="dialog" aria-label="Keyboard shortcuts">
            <p>
              <kbd>j</kbd> next change to read, <kbd>k</kbd> previous, <kbd>f</kbd> open or close all folds,{" "}
              <kbd>?</kbd> this help.
            </p>
          </div>
        )}
      </header>
      {data.files.map((file) => (
        <FileDiff key={file.path} file={file} openAll={openAll} />
      ))}
      <footer className="pr-foot">
        Base {data.base_sha.slice(0, 7)}, head {data.head_sha.slice(0, 7)}, engine {data.engine_version}.
      </footer>
    </main>
  );
}
