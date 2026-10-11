# Interrupt Package Deadlock Fix: technical notes

Skyrim AE 1.6.1170. Every address below is an Address Library ID.

## The deadlock

`41331` is an AI update that runs on a worker thread. It walks a list of actors, holding a **read** lock on the player's extra-data lock (`BSReadWriteLock` at `PlayerCharacter + 0x88`, taken at `41331+0xC8`).

For an actor whose interrupt package targets the object being processed, it calls `Actor::EndInterruptPackage` (`37474`) without releasing that lock. It does this in two places:

| Call site | Bytes before the call |
|---|---|
| `41331+0x171` | `33 D2 48 8B CB E8` (`xor edx, edx; mov rcx, rbx; call`) |
| `41331+0x36F` | the same |

Ending the package can update the player's game-only extra data: `37474 -> 39187 -> 39803 -> 20164`. `20164` takes the player's extra-data lock for **writing** and calls `AddChange(kGameOnlyExtra)`. The game's `BSReadWriteLock` write path waits for the reader count to drain, and that count includes this thread's own read. The thread therefore waits on itself, the main thread waits on it, and the process hangs.

Both sites were seen in debugger snapshots of a frozen game, with a follower in combat:
- 2026-10-06: the `+0x171` path;
- 2026-10-07: the `+0x36F` path, with 1.0 (which covered only `+0x171`) installed. The frozen thread's stack was `41331+0x374 -> 37474 -> 39187 -> 39803 -> 20164`, waiting to write the lock it had read-locked at `41331+0xC8`.

## The fix

Each call site is checked first:
- the six bytes before the call must match;
- the `E8` call must resolve to `37474`.

It is then rewritten with `SKSE::Trampoline::write_call<5>` to `DeferredEndInterruptPackage`, which:
- takes a `NiPointer<Actor>` so the actor stays alive;
- queues an SKSE main-thread task that calls the original `EndInterruptPackage(actor, flag)` once the AI update has released the player's lock;
- skips the call if the actor has been deleted by then.

A site that doesn't match is left unpatched and logged. Nothing is serialized.

## Behavioural difference

The interrupt package ends on the next main-thread task pass instead of mid-update, which is one frame later at most. No other code path changes.

## Build

```
git submodule update --init --recursive
xmake f -P . -y -p windows -a x64 -m release --skyrim_ae=y --skyrim_se=n --skyrim_vr=n
powershell -ExecutionPolicy Bypass -File scripts\package.ps1
```
