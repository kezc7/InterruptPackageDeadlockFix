# Changelog

## Unreleased (draft; gameplay validation pending)

- Add a guarded SE 1.5.97 path covering both interrupt-package calls and the
  additional virtual `EndDialogue` call at `40321+0x34C`, which was observed
  attempting a write lock while its ancestor held the player's read lock.
- Preserve the existing AE path; do not infer an AE dialogue-call offset.
- Add sanitized SE diagnostic evidence, instruction-window and deferral tests,
  and optional Windows native trampoline/loader tests.
- Retain pending actors safely and invalidate SE work across save loads/new games.
- No post-fix gameplay success is claimed; this is not a release announcement.

## 1.0.1 (2026-10-11)

Licence and source only; the code is unchanged.

- Relicensed to GPL-3.0-or-later with the Modding Exception, the terms of CommonLibSSE-NG v7, which the plugin is built with. Earlier releases said MIT by mistake.
- The source is public: https://github.com/kezc7/InterruptPackageDeadlockFix
- The download includes the licence (`InterruptPackageDeadlockFix-LICENSE.txt`) and the exceptions (`InterruptPackageDeadlockFix-EXCEPTIONS.md`).

## 1.0.0 (2026-10-11)

First public release. It defers `Actor::EndInterruptPackage` at both places in the AI pass (`41331+0x171` and `+0x36F`) to the main thread, out of the player's extra-data read lock, with these safeguards:

- **Runtime check:** the plugin now runs only on Skyrim AE 1.6.1170. The check comes before `SKSE::Init`, so another runtime's Address Library is never loaded. Before this, SKSE would load it on any AE runtime, and the SE and VR loaders were accepted at query time.
- **All-or-nothing install:** both call sites are checked before either is patched. If one doesn't match, neither is patched. Deferring only the first site keeps the NPC's interrupt package in place for the rest of the AI pass, and that can lead to the second site.
- The call that gets deferred is fixed to `Actor::EndInterruptPackage`, verified at both sites, and set before patching.
- The plugin doesn't install if SKSE provides no task interface.
- **Log:** each deferral names its call site (`+0x171` / `+0x36F`). Deferred calls skipped because the NPC was deleted are logged. A mismatch prints the bytes found. The game version is logged at startup.
- The DLL's file version is set (it was `0.0.0.0`). It now declares post-629 struct use instead of "no structs".
- Unit tests for the call-site check, using the real 1.6.1170 bytes (`callsite-tests`).

## Before release

Private development builds, not published:

- **Build 2 (2026-10-07):** also defers the second `EndInterruptPackage` call in the AI pass (`41331+0x36F`). A game still froze there with 1.0.0 installed.
- **Build 1 (2026-10-06):** first version. Defers `Actor::EndInterruptPackage` at `41331+0x171` to the main thread, out of the player's extra-data read lock.
