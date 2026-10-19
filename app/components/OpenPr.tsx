"use client";

import { useState } from "react";
import { useRouter } from "next/navigation";

export default function OpenPr() {
  const router = useRouter();
  const [value, setValue] = useState("");
  const [error, setError] = useState<string | null>(null);
  return (
    <form
      className="open-form"
      onSubmit={(event) => {
        event.preventDefault();
        const match = value.trim().match(/github\.com\/([\w.-]+)\/([\w.-]+)\/pull\/(\d+)/);
        if (!match) {
          setError("Paste a link like https://github.com/owner/repo/pull/123.");
          return;
        }
        router.push(`/pr/${match[1]}/${match[2]}/${match[3]}`);
      }}
    >
      <label htmlFor="pr-url">Pull request link</label>
      <div className="open-row">
        <input
          id="pr-url"
          value={value}
          onChange={(e) => {
            setValue(e.target.value);
            setError(null);
          }}
          placeholder="https://github.com/owner/repo/pull/123"
          autoComplete="off"
        />
        <button type="submit">Analyse</button>
      </div>
      {error && <p className="form-error">{error}</p>}
    </form>
  );
}
