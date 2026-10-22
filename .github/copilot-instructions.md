# Dayspeck: instructions for the AI agent

## Commits and CI

- The CI workflow runs a full firmware build and tests on every push that touches code. Avoid needless runs.
- Docs-only changes (`docs/`, `plans/`, `overrides/`, `mkdocs.yml`, `requirements-docs.txt`, `*.md`, `LICENSE`) need no `[skip ci]`: CI ignores them by path.
- For intermediate or WIP commits on a feature branch that do not need a build, add `[skip ci]` to the end of the commit message subject, e.g. `Tweak rain speed [skip ci]`.
- Never use `[skip ci]` on commits to `main`, on tagged commits, or on commits that should be tested before merging.
- When unsure whether a commit needs CI, ask the user instead of adding `[skip ci]`.
