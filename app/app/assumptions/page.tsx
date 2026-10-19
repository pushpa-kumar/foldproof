import Link from "next/link";
export const metadata = { title: "What verified assumes" };

export default function Assumptions() {
  return (
    <main className="prose-page">
      <p className="crumbs">
        <Link href="/">Foldproof</Link> / assumptions
      </p>
      <h1>What &ldquo;verified equivalent&rdquo; assumes</h1>
      <p>
        A fold is hidden only when libclang shows that the file you review and the file in the PR mean the same
        thing. That claim rests on a few assumptions, listed here so you can decide whether they hold for your
        codebase.
      </p>
      <h2>Inherent to the approach</h2>
      <ul>
        <li>
          Line numbers are not behaviour. Reformatting shifts <code>__LINE__</code> and assert messages; moves that
          observe <code>__LINE__</code> or <code>__COUNTER__</code> are refused.
        </li>
        <li>
          The file is analysed alone, with <code>-std=c++20</code> and the macOS SDK. Project flags, other
          <code>-D</code> definitions and generated headers are not known. Macros defined under another
          <code>#if</code> branch make a rename unfoldable.
        </li>
        <li>Missing headers cause parse errors. Renames and moves are only folded inside functions clang parsed cleanly.</li>
        <li>Nothing outside the file is checked: a move cannot change another translation unit, but ODR violations elsewhere are out of scope.</li>
      </ul>
      <h2>What is never folded</h2>
      <ul>
        <li>Renames of globals, members, types or anything with linkage.</li>
        <li>Renames in functions that use macros which stringize, paste, or mention the old or new name.</li>
        <li>Whitespace changes inside macro arguments that could be stringified, inside directives, or in #if-disabled code.</li>
        <li>Moves of templates, methods, or functions that cross a #pragma or #define region.</li>
        <li>Anything mixed with a real change: the whole hunk stays visible.</li>
      </ul>
      <h2>How it was tested</h2>
      <p>
        Each checker went through an independent adversarial review, and every confirmed hole became a regression
        test. A generated suite of near-miss edits is compiled and run: for every fold Foldproof accepts, the
        reviewed program and the PR program must print the same output.
      </p>
    </main>
  );
}
