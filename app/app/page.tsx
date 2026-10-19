import { readDemoIndex } from "@/lib/demos";
import OpenPr from "@/components/OpenPr";

export default async function Home() {
  const demos = await readDemoIndex();
  return (
    <main className="home">
      <header className="home-head">
        <h1 className="wordmark">Foldproof</h1>
        <p className="lede">
          Coding agents bury the lines that matter under renames, moved functions and reformatting. Foldproof
          folds that churn away in C++ pull requests, but only after clang verifies each fold changes nothing.
          What it cannot verify, it leaves in front of you and says why.
        </p>
      </header>

      <section className="demos" aria-labelledby="demos-title">
        <h2 id="demos-title">Agent-written pull requests</h2>
        <ol className="demo-list">
          {demos.map((demo) => {
            const share = demo.changed ? demo.residual / demo.changed : 1;
            return (
              <li key={`${demo.owner}/${demo.repo}/${demo.number}`}>
                <a className="demo" href={`/pr/${demo.owner}/${demo.repo}/${demo.number}`}>
                  <span className="demo-title">{demo.title}</span>
                  <span className="demo-repo">
                    {demo.owner}/{demo.repo} #{demo.number}
                    {demo.agent ? `, by ${demo.agent.replace("_", " ")}` : ""}
                  </span>
                  <span className="demo-ratio" aria-hidden="true">
                    <span className="ratio-read" style={{ width: `${Math.max(share * 100, 0.6)}%` }} />
                    <span className="ratio-folded" />
                  </span>
                  <span className="demo-count">
                    {demo.residual} of {demo.changed} lines to read. {demo.note}
                  </span>
                </a>
              </li>
            );
          })}
        </ol>
      </section>

      <section className="open" aria-labelledby="open-title">
        <h2 id="open-title">Open any C++ pull request</h2>
        <p>Runs locally with the native engine; the hosted site shows the examples above.</p>
        <OpenPr />
      </section>
    </main>
  );
}
