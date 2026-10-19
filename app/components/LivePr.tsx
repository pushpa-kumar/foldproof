"use client";

import Link from "next/link";
import { useEffect, useState } from "react";
import PrView from "./PrView";
import type { PrData } from "@/lib/types";

export default function LivePr({ owner, repo, number }: { owner: string; repo: string; number: string }) {
  const [data, setData] = useState<PrData | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    fetch(`/api/analyze?owner=${encodeURIComponent(owner)}&repo=${encodeURIComponent(repo)}&number=${number}`)
      .then(async (res) => {
        const body = await res.json();
        if (!res.ok) throw new Error(body.error ?? `Analysis failed with status ${res.status}`);
        setData(body);
      })
      .catch((err: Error) => setError(err.message));
  }, [owner, repo, number]);

  if (data) return <PrView data={data} />;
  return (
    <main className="pr">
      <p className="crumbs">
        <Link href="/">Foldproof</Link> / {owner}/{repo} #{number}
      </p>
      {error ? (
        <p className="banner">{error}</p>
      ) : (
        <p className="loading">Fetching the PR and checking every fold with clang. Large PRs take a minute.</p>
      )}
    </main>
  );
}
