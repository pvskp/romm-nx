# AGENTS.md

Guidelines for AI agents (and humans) working in this repo.

## Committing

Commit only when the user explicitly reports that a feature or fix they
requested is working ("está funcionando", "funciona", "ok", "ótimo",
"perfeito", "commite", etc.). A successful build is necessary but NOT
sufficient — a green build never triggers a commit by itself. Split
unrelated changes into separate commits (one logical change per commit) and
exclude `TODO.txt` unless the user asks otherwise. Before committing, build
the project (see below) and verify with `git diff --cached --check`.

## Build / toolchain

This is a Nintendo Switch homebrew project built with
[devkitPro](https://devkitpro.org/) (`devkitA64` + `libnx`). The toolchain is
already installed on in the path stored in `DEVKITPRO`, so a plain build works
without extra setup:

```
make -j$(nproc)                # default: Tico flavor -> build-tico/romm-nx-tico.nro
make FRONTEND=retroarch -j$(nproc)  # RetroArch flavor -> build-retroarch/romm-nx-retroarch.nro
make clean                     # remove both build dirs and all .nro/.elf/.nacp artifacts
```

The build serves exactly one frontend per flavor (compile-time:
`ROMM_FRONTEND_TICO` / `ROMM_FRONTEND_RETROARCH`), selected through
`source/model/FrontendProfile.hpp`. Each flavor only compiles its own catalog
and frontend profile translation unit, so neither build contains the other
frontend's code or folders. Run BOTH flavors before committing a build-affecting
change.

The project uses C++20 (`-std=gnu++20`) with exceptions and RTTI disabled
(`-fno-rtti -fno-exceptions`), so new code must not rely on RTTI (no
`dynamic_cast` / `typeid`) or throw/catch exceptions.

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
