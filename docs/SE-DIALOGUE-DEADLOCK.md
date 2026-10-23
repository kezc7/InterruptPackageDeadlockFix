# SE 1.5.97: dialogue cleanup under the player's read lock

**Draft contribution. Gameplay validation is pending.** The engine failure
was observed in a real freeze. The proposed fix has offline tests; these do
not establish in-game compatibility or long-session stability. The existing
AE 1.6.1170 interrupt-package path is retained. No AE dialogue offset is
inferred from the SE layout.

## Observation and trigger

Two minidumps taken approximately six seconds apart showed the same engine
worker stalled, while the main thread waited for task completion. The player
was travelling outdoors on horseback with Gorr from Interesting NPCs.

The reference passed to `TESObjectREFR::Disable` was the third NFF follower
horse: `nwsFollowerFramework.esp`, local reference `034A93` (`nwsHorseREF3`),
base NPC `034A85` (`nwsFF_Horse_3`). During cleanup, the engine's actor loop
reached Gorr and invoked `Actor::EndDialogue` while holding the player's
extra-data read lock.

This identifies the disabled object and the deadlock; it does not identify
the originating Papyrus event. Read-only inspection of effective installed
scripts found ordinary horse-disable calls in NFF's `DisableHorse`,
`RefreshHorse`, and dismissal handling. An Animated Whistling override leaves
those functions unchanged. It is not evidence that the player dismounted,
dismissed a follower, or whistled, nor that Interesting NPCs is defective.

## Engine evidence (module-relative addresses only)

| Item | Skyrim SE 1.5.97 |
| --- | --- |
| Cleanup function | Address Library `40321`, RVA `0x6D7330` |
| Player global | RVA `0x2F26EF8`; resolved reference form ID `0x14` |
| Guarded lock | Player reference + `0x80` |
| Read acquisition | RVA `0x6D73F3`, calling read-acquire at `0xC072D0` |
| Saved guard pointer | `[rbp+7]` in the cleanup frame |
| Dialogue call | `40321+0x34C`, RVA `0x6D767C` |
| Dialogue instruction | `FF 90 D0 06 00 00`: virtual slot `0xDA` |
| Dialogue implementation | Address Library `36551`, RVA `0x5EA900` |
| Nested write request | RVA `0x2A5BA7`, on the same player + `0x80` lock |
| Worker stuck in writer acquisition | RVA `0xC073AB` |
| Guard release not reached | RVA `0x6D7817`, calling `0xC07590` |
| First interrupt call | `40321+0x172`, RVA `0x6D74A2` |
| Second interrupt call | `40321+0x36A`, RVA `0x6D769A` |
| Interrupt callee | Address Library `36475`, RVA `0x5E27F0` |

Inner-to-outer engine stack RVAs:

```text
C073AB -> 2A5BAC -> 67A298 -> 645792 -> 5EAACA -> 6D7682
       -> 29892D (Disable) -> 5C7E66 (task dispatch)
```

The saved guard pointer in the ancestor frame matched the lock requested by
the nested writer. Three read-only observations over several minutes showed
writer thread zero and reader count one. Captured read-acquire/release and
write-acquire instructions confirm the counter interpretation: this writer
cannot upgrade the read lock held by its own ancestor. The main thread was
waiting for the worker task rather than making progress through loading.

Instructions were captured from the live process read-only and disassembled
in an offline dump view. The on-disk executable's packed bytes and community
symbol names were not used as sole evidence. A separate write-unlock hook
exists in that setup but was not reached; this report does not claim the
whole engine was unmodified or attribute the failure to that hook.

## Proposed change

For **SE 1.5.97 only**, defer all three calls through SKSE's main-thread task
interface. The dialogue branch preserves virtual dispatch. Each task retains
the actor with `NiPointer`, skips a deleted actor, and discards work queued
before a save-load/new-game generation change. Nothing is serialized.

Before any call site is patched, verify the function/callee RVAs and all
captured entry, lock, argument, call, and branch windows. Any mismatch leaves
all three sites unchanged. In particular, installing only the first matching
site before discovering a mismatch in the dialogue branch is forbidden.
No global lock manipulation, actor deletion, follower dismissal, or save
cleaning is involved.

The public baseline contains only AE support. The author's separately
published optional 1.1.0 SE build documents two interrupt-package sites;
the observed virtual dialogue call is an additional path. This draft may
need rebasing onto the author's unpublished SE source before integration.

## Tests and validation limits

- Existing AE call-site tests remain unchanged.
- `se-cleanup-tests` uses small, independent capture excerpts. It checks
  every guarded byte, callee/function mismatches, truncated input, and zero
  writes on rejection. It exercises deferred lock acquisition, actor
  lifetime/release, deletion, save-load/new-game invalidation, null input,
  and preservation of the interrupt-package argument.
- `native-cleanup-tests` runs real CommonLib five- and six-byte trampolines
  against synthetic executable code, then loads the built DLL to check its
  exports and runtime gates. It does **not** launch Skyrim or execute game
  instructions.
- The original local candidate passed 251 offline checks against the larger
  private capture before this upstream adaptation. That count is not a
  claim of 251 gameplay scenarios or validation of this PR revision.

Pending: normal SKSE startup and three-site activation in the actual modded
game, horse/follower travel, conversations, save reloads, and longer play.
No successful post-fix gameplay result is claimed.

Deferral changes cleanup timing, typically until the next main-thread task
pass. A new dialogue/package can begin during that interval and may then be
ended by the deferred call. Duplicate calls are not coalesced. These are
review and gameplay-validation concerns, not proven harmless behavior.

## Sanitization

This report includes engine-relative offsets, public plugin-local record
identities and small instruction excerpts needed for validation. No dumps,
saves, full logs, process addresses/IDs, local filesystem paths, account
information, credentials, or complete engine-code captures are included.
