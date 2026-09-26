# romm-nx

**romm-nx** is an unofficial Nintendo Switch homebrew client for
[RomM](https://github.com/rommapp/romm).

It allows you to browse your RomM library and sync games, saves and states
from your own RomM server directly to your Nintendo Switch.

> [!IMPORTANT] romm-nx is a personal project intended as a temporary solution
> until a more official or robust alternative becomes available.
>
> It is **not affiliated with, maintained by, or officially supported by the
> RomM team**.

## Fork of r4lix/romm-nx

This repository is a fork of [r4lix/romm-nx](https://github.com/r4lix/romm-nx),
the original unofficial RomM client for the Nintendo Switch. It is maintained
independently and diverges significantly from upstream.

**What this fork adds or changes:**

- **Sync to a frontend of your choice** — the app is built per frontend: a
  **Tico** build pushes your RomM library into Tico's folders (ROMs, saves,
  covers and an optional per-game background) and a **RetroArch** build syncs
  ROMs, saves and save states into RetroArch's own folders. Each build is a
  separate app with its own config, sync state and update channel, so both can
  be installed side by side.
- **Save Data screen** — local-first save management per game and per platform,
  with a verdict chip (IN SYNC / LOCAL ONLY / SERVER ONLY / LOCAL NEWER / SERVER
  NEWER / CONFLICT), a LOCAL → LAST SYNC → SERVER time strip, and SYNC / UPLOAD
  / DOWNLOAD actions.
- **State Data screen** — manage save states
  (`<base>/states/<platform>/<game>.state1..9`) per game and per platform,
  mirroring the Save Data screen.
- **Sync workflow** — pre-flight options modal, conflict resolution (keep local
  / keep server / skip), saves-only and states-only modes, and a progress modal
  that reports every stage (including the background copy).
- **Library bulk flow** — `X` marks games (the selection survives browsing
  across platforms) and `ZR` opens the sync pre-flight for the marked set; the
  sidebar and hints echo the selection.
- **Detail view** — manual cover/screenshot switch (R-Stick), a single Details
  tab, and scroll-tip chevrons for long descriptions.
- **Simplified UI** — removed the classic in-app download queue, the
  Installed / File Browser screens and the platform-visibility system; the main
  menu is now Games / Save Data / States / Settings; every platform the server
  reports is always listed.

The features still planned upstream — cheats/mods management and an in-app game
launcher — are **not** part of this fork.

## Current features

- Connect to a self-hosted RomM server
- Browse platforms and games, with cover art and details
- Sync your library into the built frontend's folders (Tico: ROMs, saves,
  covers and backgrounds; RetroArch: ROMs, saves and states) via the sync
  workflow
- Manage saves per game and per platform (Save Data screen)
- Manage save states per game and per platform (State Data screen)
- Mark games in the library and bulk-sync the selection

Some features are still experimental or incomplete.

## To-do

- Cheats and mods management
- In-app game launcher

## Build from source

The frontend is chosen at build time; each flavor produces its own NRO.

```bash
make -j$(nproc)            # Tico build:  romm-nx-tico.nro
make FRONTEND=retroarch -j$(nproc)   # RetroArch build: romm-nx-retroarch.nro
# or: make retroarch
```

## Installation

1. Download the `.nro` of the frontend you use (or both, to manage both).
2. Copy each one to its own folder:

```text
sdmc:/switch/romm-nx-tico/romm-nx-tico.nro        (Tico build)
sdmc:/switch/romm-nx-retroarch/romm-nx-retroarch.nro   (RetroArch build)
```

3. Launch romm-nx from the Homebrew Menu.
4. Configure the address and credentials of your RomM server.

Optionally, after launching the application for the first time, a configuration
file will be created at:

```text
sdmc:/switch/romm-nx-tico/config.json        (Tico build)
sdmc:/switch/romm-nx-retroarch/config.json   (RetroArch build)
```

Each build keeps its own configuration and sync-state records. If you upgrade
from an older combined build, the first sync adopts the records that belonged
to this frontend from the old `sdmc:/switch/romm-nx/sync_state.json`.

A self-hosted RomM server is required.

## Legal notice

romm-nx does not provide, host, or distribute games.

You are responsible for the content stored on your RomM server and for complying
with the laws applicable in your country.

## Issues and contributions

Bug reports and feedback are welcome.

Because I am not a developer, I may not be able to fix every issue or provide
technical support. Pull requests and contributions from experienced developers
are greatly appreciated.

When reporting an issue, please include as much information as possible, such as
the romm-nx version, your RomM version, the affected platform, and any available
logs.

## Thanks and credits

- [RomM](https://github.com/rommapp/romm) for the original self-hosted ROM
  manager
- The Nintendo Switch homebrew community
- The open-source projects and libraries used by romm-nx
- The AI coding tools used to help build the project: Antigravity, Claude Code,
  and ChatGPT
