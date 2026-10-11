# Interrupt Package Deadlock Fix

An SKSE plugin for Skyrim AE 1.6.1170 that fixes a vanilla engine freeze.

**The freeze:** an AI update on a worker thread (ID 41331) holds a read lock on the player's extra data, then ends an actor's interrupt package (ID 37474) without releasing it. Ending the package can write the player's extra data. The write waits for every reader, including its own thread's read, so the thread waits on itself and the game freezes. It was seen with a follower in combat.

**The fix:** both call sites in 41331 are redirected, each checked byte for byte first. The package is then ended a moment later on the main thread, after the lock is released.

Traced in game on 2026-10-06 and 2026-10-07. Full notes are at the top of `src/main.cpp`.

## Build
```
xmake f -P . -y -p windows -a x64 -m release --skyrim_ae=y --skyrim_se=n --skyrim_vr=n
xmake build -y
```
After a clone, fill the submodule first with `git submodule update --init --recursive`.

Release: `scripts\package.ps1` builds and zips the mod into `dist/`. Technical notes: [docs/TECHNICAL.md](docs/TECHNICAL.md). Nexus page text: [docs/NEXUS.txt](docs/NEXUS.txt).
