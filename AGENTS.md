# AGENTS.md

Guidelines for AI agents (and humans) working in this repo.

## Committing

Commit only when the user explicitly reports that a feature or fix they
requested is working ("está funcionando", "funciona", "ok", "ótimo",
"perfeito", "commite", etc.). Passing tests and lint are necessary but NOT
sufficient — a green test run never triggers a commit by itself. Split
unrelated changes into separate commits (one logical change per commit) and
exclude `TODO.txt` unless the user asks otherwise. Before committing, run
`make test` and `make lint` and verify with `git diff --cached --check`.

## Commit messages

Use [Conventional Commits](https://www.conventionalcommits.org/). Format:

```
<type>(<scope>): <short imperative summary>
```

- Types: `feat`, `fix`, `refactor`, `docs`, `test`, `chore`, `build`, `perf`.
- Scope (optional): package or area, e.g. `feat(search)`, `fix(detail)`.
- Short summary in imperative mood, lowercase, no trailing period.
- One logical change per commit. Don't mix unrelated work.
- The repo language for code is English; UI strings are English. TODO.txt and
  this file may stay in Portuguese.
