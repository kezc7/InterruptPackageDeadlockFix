# Interrupt Package Deadlock Fix: technical notes

Skyrim AE 1.6.1170 (`SkyrimSE.exe` 1.6.1170.0, SKSE 2.2.6). Numbers in the form `41331` are Address Library IDs. In 1.6.1170, `41331` is RVA `0x76C2C0` and `37474` is RVA `0x6745C0`.

## The deadlock

`41331` is an AI pass that runs on a worker thread. It walks a process list of actors (`[rcx+0x90]`). For each actor, it looks the actor up through its handle into a smart pointer held at `[rsp+0x20]`. It then read-locks the player's extra-data lock (`BSReadWriteLock` at `PlayerCharacter+0x88`, taken by `68233` at `+0xC3`/`+0xC8`) and compares the actor's package targets with the reference being processed (`r15`).

When the actor's interrupt package targets that reference, it calls `Actor::EndInterruptPackage(actor, false)` (`37474`) while still holding the read lock. It does this in two places:

| Call site (`E8`) | RVA | Bytes from 5 before | Path to it |
|---|---|---|---|
| `41331+0x171` | `0x76C431` | `33 D2 48 8B CB E8 8A 81 F0 FF` | the current package's target (`39848` → `39855` → handle at `+0x18`) is `r15` |
| `41331+0x36F` | `0x76C62F` | `33 D2 48 8B CB E8 8C 7F F0 FF` | a later target check (`37396`, or the package data via `29869`) matches `r15`, `37739` returns false and `29504` returns true |

Ending the package can update the player's game-only extra data: `37474 → 39187 → 39803 → 20164`. `20164` takes the player's extra-data lock for **writing** and calls `AddChange(kGameOnlyExtra)`. The `BSReadWriteLock` writer waits for the reader count to drain, and that count includes this thread's own read. The thread therefore waits on itself, the main thread waits for the AI pass, and the process hangs.

Both sites were seen in debugger snapshots of a frozen game, with a follower in combat:
- 2026-10-06: the `+0x171` path.
- 2026-10-07: the `+0x36F` path, with 1.0 (which only covered `+0x171`) installed. The stack was `41331+0x374 → 37474 → 39187 → 39803 → 20164`, waiting to write the lock it had read-locked at `41331+0xC8`.

## The fix

`SKSEPlugin_Load`:
1. Refuses any runtime other than 1.6.1170 before `SKSE::Init` (which would load that runtime's Address Library). `UsesAddressLibrary` makes SKSE accept every AE runtime, as shown in SKSE's `PluginManager::checkPluginCompatibility`, so this check has to happen in the plugin. `SKSEPlugin_Query`, which only the SE and VR loaders call, declines too.
2. Checks **both** sites before writing anything. The six bytes before each call must be `33 D2 48 8B CB E8`, and each `E8`'s rel32 must resolve to `37474`. If either check fails, nothing is patched. The check is `src/CallSite.h`, unit-tested in `tests/CallSiteTests.cpp` against the bytes above.
3. Points the deferred call at `37474` (verified at both sites), then rewrites each `E8` with `SKSE::Trampoline::write_call<5>`. A 14-byte absolute-jump stub per site comes from SKSE's trampoline pool. The patch replaces exactly one 5-byte instruction, the `E8` call verified in step 2.

Each site gets its own thunk (`DeferredEndInterruptPackage<0x171>` / `<0x36F>`), so the log names the site. The thunk:
- takes a `NiPointer<Actor>`. That calls `BSHandleRefObject::IncRefCount`, which is the same refcount at `actor+0x28` (masked `0x3FF`) that 41331 itself increments and decrements around this code. The actor can't be freed until the task has run and released it, and if this was the last reference, the task frees it on the main thread, as the game does elsewhere.
- queues an SKSE task (`TaskInterface::AddTask`, which is thread-safe). The task calls `37474(actor, a_skipDialogue)` on the main thread after the AI pass has released the lock.
- skips the call if the actor has been marked deleted by then.

Nothing is serialized.

## Analysis

These answers come from source review and static reading of the 1.6.1170 code around both sites. They aren't from runtime tracing, except where the debugger snapshots above say otherwise.

**ABI and calling convention.** Both sites pass `rcx = actor` and `edx = 0`. Neither reads `rax` after the call: `+0x176` is `nop; test rdi, rdi`, and `+0x374` is `jmp +0x64C`. So a `void(Actor*, bool)` replacement is correct. Volatile registers can be clobbered across any call. `rbx` (the actor) is non-volatile, and the code reloads it from `[rsp+0x20]` anyway.

**Is the `bool` preserved?** Yes. Both sites pass `false` (`xor edx, edx`), and the thunk forwards exactly what it received. CommonLib names the parameter `a_skipDialogue`. That name is a guess and doesn't affect the fix.

**Is calling 37474 on the main thread safe?** Very likely. The main thread does a large share of actor and AI work, and CommonLib's `Actor::StopAlarmOnActor` calls `EndInterruptPackage(false)` outside any AI worker. When the task runs, the AI pass is no longer holding the player's read lock, so the write in `20164` can proceed. This hasn't been proven exhaustively. See *Lock inversion* below.

**Lifetime.** This is covered above. `NiPointer` holds a real reference on the same counter the game uses, so the memory stays valid. A deleted actor is skipped. Disabled or unloaded actors are still processed, as vanilla would at the original call.

**Behavioural difference: the timing.** The package now ends on the next SKSE task pass instead of immediately. The rest of the same 41331 call still sees the actor in its interrupt package. That changes which branches 41331 takes after `+0x171` for that actor (`+0x204` onwards). Most notably, the target check that leads to `+0x36F` can now succeed for an actor whose package vanilla would already have ended. That is why install is all-or-nothing: with only `+0x171` deferred, `+0x36F` could become reachable *more* often than in vanilla while still under the lock. It is also a plausible reason the 1.0 game froze at `+0x36F`.

**Repeated deferrals for one actor.** These are possible: one from each site in the same pass, or one from each of several 41331 calls before the task queue drains. They run in order on the main thread. Vanilla's own call count per actor in this situation isn't known, and nobody has confirmed that `37474` does nothing when the actor has no interrupt package. So the plugin doesn't merge or drop repeats, because merging them could itself change behaviour. The per-site log counters make repeats visible.

**Package changes in between.** If the actor gets a *new* interrupt package between the deferral and the task, the task ends the new one. A state check (capturing the package and comparing) would need the fields `37474` actually inspects, and those haven't been checked. The window is about one frame. This is listed as a residual risk.

**Combat and follower AI.** The only effect is the timing above: an interrupt package ending up to a frame later. Combat and follower packages aren't touched directly.

**Cell transitions, loads and shutdown.** Tasks drain every frame on the main thread, so a queued call normally runs well before the player can change cells or load a save. A call queued just before a load could run against the newly loaded state, where ending the actor's current interrupt package is at worst a lost interrupt behaviour. Tasks left at exit simply never run, and the `NiPointer` they hold isn't released, which doesn't matter at process exit.

**Lock inversion on the main thread.** For the deferred call to deadlock, the main thread would have to hold a lock that a reader of the player's extra-data lock is waiting for, while waiting to write. SKSE runs tasks from its main-loop hook, outside the AI pass. The player's extra-data lock is taken for writing on the main thread routinely in vanilla, so no new ordering is introduced. This is reasoned, not proven.

**Other plugins.**
- If a plugin patches either call site *before* this one loads, the target check fails and this plugin stands down without patching anything.
- If a plugin patches a site *after* this one loads, it captures this plugin's thunk as its "original". This keeps working if it calls through.
- A plugin that detours the start of `37474` still runs, because the task calls `37474`'s address, but for these two calls it now runs on the main thread.
- A plugin that detours the start of `41331` doesn't interact with this one.
- No published plugin is known to touch either.

## Verification status (1.2.0)

| Kind | What |
|---|---|
| Source review | All of `src/`, plus the CommonLib parts it uses: `SKSE::Init`, `TaskInterface`, `Trampoline::write_call`, `BSHandleRefObject`. |
| Static verification | The code bytes at both sites and their callers in 1.6.1170 (above). SKSE 2.2.6's compatibility check (`PluginManager.cpp`). The built DLL's `SKSEPlugin_Version` block: version 1.2.0.0, `versionIndependence = 0x5`. |
| Unit tests | `callsite-tests`: 8 checks, all passing. |
| Debugger | The two freeze snapshots in 1.0.0 and earlier, from before this release. |
| In game | 1.1.0 loaded and installed at both sites on 1.6.1170 (log from 2026-10-11). **1.2.0 hasn't been run in game yet.** See [TESTING.md](TESTING.md). |
