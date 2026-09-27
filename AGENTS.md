<!-- SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Instructions for coding agents

These instructions apply throughout this repository. Read [CONTRIBUTING.md](CONTRIBUTING.md) found at the root of this repository. It defines the coding standards, formatting, static analysis and documentation rules and policies applicable to this project.  Do not change the rules: apply them as defined in CONTRIBUTING.md. Any code changes must be documented, conform to the clang-format and clang-tidy settings as described in CONTRIBUTING.md.

## Documentation

- Write comments and documentation in plain, direct language, using short sentences
  and common words. Say what the code does and why, and note any constraint a
  reader would not guess; don't repeat what the code already shows. Use "is" and
  "has" instead of "serves as", "stands as", "boasts" or "features". Don't use
  promotional or inflated words such as "robust", "seamless", "powerful",
  "comprehensive", "crucial", "pivotal", "delve", "leverage", "enhance", "showcase",
  "underscore" or "testament". Avoid metaphors and figurative phrasing, and name
  the actual mechanism instead. Don't write "not just X, but Y" constructions, lists
  of three items by habit, trailing "-ing" clauses that claim significance ("ensuring
  reliability", "highlighting the importance of"), or closing sentences that summarize
  or praise what came before. Keep formatting minimal: no emoji, no bold for emphasis,
  no em dashes as general-purpose punctuation, and no headings or bullet lists where a
  sentence would do.  Don't address the reader ("Let's", "Happy coding") or describe
  your own editing choices in the text. If deleting a sentence would cost the reader
  no information, delete it.

## Commit sign-off

Follow [Commit Sign-Off](CONTRIBUTING.md#commit-sign-off): every commit must
contain a `Signed-off-by: Name <email>` trailer. When creating a commit, use:

The human contributor must review and understand all AI-assisted changes as required by
`CONTRIBUTING.md`.  Commiting code must be initiated by the contributor, not a coding agent.

## Verification and reporting

After code changes, rebuild the affected targets and run relevant existing
tests. Add regression coverage for behavior changes when needed. Run the full
suite for broad changes such as dependency updates. Report which checks ran,
their results, and any failures or unavailable tools; do not claim checks passed
without running them. Documentation-only edits need a content and diff review,
not an unrelated rebuild.
