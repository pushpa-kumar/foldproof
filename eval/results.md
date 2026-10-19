# Corpus results

85 agent-authored PRs, 415 modified C++ files (406 analysed, 9 fell back to a plain diff).

| method | lines to review | vs raw |
|---|---:|---:|
| raw diff (git) | 18283 | 100.0% |
| git diff -w | 15569 | 85.2% |
| git diff -w --color-moved | 14069 | 77.0% |
| difftastic | 15851 | 86.7% |
| Foldproof | 15342 | 83.9% |

Files with at least one verified fold: 129 of 406.
Folds by kind: {'format': 1670}.

Most common refusal reasons:

- 102 × '…' became '…' (not a local variable)
- 20 × time budget ran out before this fold was checked
- 14 × whitespace changed inside a macro argument (may be stringified)
- 13 × '…' now resolves to a different operator or function
- 13 × comment text changed
- 6 × '…' would report a different line number
- 4 × compiler errors differ between the reviewed and the new file
- 3 × '…' now refers to a different entity

Analysis time per file: median 0.69 s, p90 4.58 s, max 35.83 s (release build, no include paths).
