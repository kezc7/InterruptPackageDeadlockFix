# Interrupt Package Deadlock Fix

An SKSE plugin that fixes a vanilla engine **freeze** (a deadlock) in Skyrim Anniversary Edition **1.6.1170**. It has mostly been seen with a follower in combat.

> **Status: 1.0.0, first public release.** The freeze and the fix were both traced with a debugger in a real game, and the fix has stepped in there. Reports from more setups are welcome. See [Known limitations](#known-limitations).

## Requirements

| | |
|---|---|
| Skyrim | **AE 1.6.1170 only** (Steam). Any other runtime is refused, and the plugin changes nothing there. |
| [SKSE64](https://skse.silverlock.org/) | The build for 1.6.1170 (2.2.6). |
| [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444) | The "All in one (Anniversary Edition)" file. |

**Not supported:** Skyrim SE 1.5.97, Skyrim VR, GOG, and every other AE runtime (1.6.640, 1.6.1130 and so on). The patched code was only checked against 1.6.1170.

## Installation

**Mod Organizer 2:** *Install a new mod from archive*, pick `InterruptPackageDeadlockFix-1.0.1.zip`, enable it in the left pane, and launch through SKSE (`skse64_loader.exe`).

**Vortex:** drag the zip onto the *Mods* page (or *Install From File*), enable it and deploy. Launch through SKSE.

**By hand:** copy `SKSE\Plugins\InterruptPackageDeadlockFix.dll` into `Skyrim Special Edition\Data\SKSE\Plugins\`.

There's no ESP, no MCM and no configuration. It doesn't write to your save, so it can be added or removed mid-playthrough.

## What it fixes

**In plain English:** while the game updates NPC AI on a background thread, it holds a "read" lock on the player's data. In two places it then cancels an NPC's *interrupt package* (the short behaviour an NPC switches to when something interrupts them) without letting go of that lock. Cancelling it can need to *write* the player's data, and a write has to wait until every reader has finished, including the thread that is waiting. That thread waits on itself, the main thread waits on it, and the game freezes. The picture stops, the sound may loop, and only Task Manager gets you out.

**The fix:** those two calls are redirected. The NPC's interrupt package is cancelled a moment later on the main thread, once the AI update has let go of the lock. The NPC is kept in memory until then. Nothing else is changed.

**Technical summary:** `41331` (an AI pass run on a worker thread) read-locks `PlayerCharacter+0x88` (`BSReadWriteLock`) at `+0xC8`. It then calls `Actor::EndInterruptPackage` (`37474`) at `+0x171` and `+0x36F` while still holding the lock. `37474 → 39187 → 39803 → 20164` write-locks the same lock, and `BSReadWriteLock`'s writer waits for the reader count to reach zero, so the thread waits on its own read. The plugin rewrites both `E8` calls so they queue an SKSE main-thread task, which calls `37474` with the same arguments. Full notes: [docs/TECHNICAL.md](docs/TECHNICAL.md).

## Safety checks

- **Runtime:** the plugin loads only on 1.6.1170. Older loaders (SE and VR) are declined at query time. Other AE runtimes are declined at load, with a line in the log.
- **Call sites:** before anything is written, both sites must be exactly `xor edx, edx; mov rcx, rbx; call <Actor::EndInterruptPackage>`. If either one isn't (another mod got there first, or the game is different), **neither** is patched and the log says why.
- **Actor lifetime:** the NPC is held through the game's own reference count until the deferred call has run. If it has been deleted by then, the call is skipped.

## Known limitations

- **Timing:** the interrupt package now ends on the next main-thread task pass, usually within a frame, instead of immediately. For the rest of that AI pass, the game still sees the NPC in its interrupt package. This is the core trade-off of the fix. It hasn't caused visible problems so far, but it isn't vanilla timing.
- **Repeated calls:** because the package is still in place for the rest of that pass, the same NPC can occasionally be queued twice, once from each call site. Both calls then run in order on the main thread. Whether the second one is always a no-op hasn't been confirmed, so they aren't merged.
- **A new interrupt package in that window:** if an NPC gets a new interrupt package between the deferral and the main-thread call, the deferred call ends the new one. The window is about one frame.
- **Other mods hooking the same code:** a mod that patches either call site *before* this one loads makes this plugin stand down (the log says so). A mod that hooks `Actor::EndInterruptPackage` itself still works, but for these two calls its hook now runs on the main thread instead of an AI thread. No such mod is known.
- This fixes **one** specific freeze. Other freezes have other causes.

## Compatibility

- **Works alongside** other engine-fix plugins that don't touch these two calls. The author's own setup runs it with Skyrim Freeze Fix NG, which fixes a different freeze.
- **Load order** doesn't apply (it has no plugin file).
- **Save games:** nothing is serialized.

## Troubleshooting

The log is at:

```
Documents\My Games\Skyrim Special Edition\SKSE\InterruptPackageDeadlockFix.log
```

| Log says | Meaning |
|---|---|
| `installed at SkyrimSE+76C431 (41331+0x171 …)` and `… +0x36F …` | Working. Both sites are patched. |
| `+0x171: deferred EndInterruptPackage #N for XXXXXXXX …` | The fix stepped in (shown for the first 20, then every 100th, per site). `XXXXXXXX` is the NPC's form ID. |
| `skipped deferred EndInterruptPackage … deleted` | The NPC was deleted before the main thread got to it. This is harmless. |
| `not installed: only Skyrim 1.6.1170 is supported` | Wrong game version. Nothing was changed. |
| `not installed: 41331+0x… doesn't match` / `doesn't go to Actor::EndInterruptPackage` | Another mod patched the same spot first. Nothing was changed. Please report the mod. |

If there is **no log at all**, check `skse64.log` in the same folder for an `InterruptPackageDeadlockFix` line. It will say whether SKSE loaded the plugin, or why it didn't (Address Library missing, wrong game version).

**Reporting a freeze while this is installed:** please include this log, `skse64.log`, your mod list and, if you can, a full memory dump of the frozen process (Task Manager → right-click `SkyrimSE.exe` → *Create memory dump file*).

## Version history

See [CHANGELOG.md](CHANGELOG.md). 1.0.0 is the first public release; the development builds before it are listed there.

## Building

Requires Visual Studio 2022 (MSVC) and [xmake](https://xmake.io) 3.0.1+.

```
git submodule update --init --recursive
xmake f -P . -y -p windows -a x64 -m release --skyrim_ae=y --skyrim_se=n --skyrim_vr=n
xmake build -y InterruptPackageDeadlockFix
xmake build -y callsite-tests && xmake run callsite-tests
```

`scripts\package.ps1` builds the plugin, runs the tests and writes `dist\InterruptPackageDeadlockFix-<version>.zip`.

- [docs/TECHNICAL.md](docs/TECHNICAL.md): the deadlock, the fix and its analysis

## Licence

GPL-3.0-or-later with the Modding Exception: see [LICENSE](LICENSE) and [EXCEPTIONS.md](EXCEPTIONS.md). These are the same terms as [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG), which Interrupt Package Deadlock Fix is built with. Source: https://github.com/kezc7/InterruptPackageDeadlockFix
