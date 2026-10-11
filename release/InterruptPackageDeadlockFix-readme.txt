Interrupt Package Deadlock Fix 1.0.1
Skyrim AE 1.6.1170 ONLY - needs SKSE64 and Address Library for SKSE Plugins

Fixes a freeze in the vanilla engine (a deadlock), often seen with a
follower in combat.

THE FREEZE
While the game updates NPC AI on a background thread, it holds a read lock
on the player's data. In two places it then cancels an NPC's "interrupt
package" (the short behaviour an NPC switches to when something interrupts
them) without letting go of that lock. Cancelling it can need to write the
player's data, and a write waits for every reader to finish - including the
thread that is waiting. The thread waits on itself, the main thread waits
on it, and the game freezes.

THE FIX
Those two calls are redirected: the interrupt package is cancelled a moment
later (usually within a frame) on the main thread, once the lock has been
released. The NPC is kept safely in memory until then. Nothing else in the
game is changed.

SAFETY
- Runs only on Skyrim 1.6.1170; on any other version it does nothing and
  says so in its log.
- Both patched spots are checked byte for byte first. If either isn't
  exactly what is expected (e.g. another mod changed it), neither is
  patched, and the log says why.
- Nothing is written to your save, so it can be added or removed at any
  time.

INSTALL
Mod Organizer 2 / Vortex: install this archive, enable it, launch through
SKSE. By hand: copy SKSE\Plugins\InterruptPackageDeadlockFix.dll into
Skyrim's Data\SKSE\Plugins folder. Remove it the same way.

LOG
Documents\My Games\Skyrim Special Edition\SKSE\InterruptPackageDeadlockFix.log
- "installed at ..." (twice): working.
- "deferred EndInterruptPackage #N ...": the fix stepped in (the first 20
  times per spot, then every 100th).
- "not installed: ...": the reason follows; nothing was changed.
No log at all? Look for "InterruptPackageDeadlockFix" in skse64.log in the
same folder.

Not for Skyrim SE 1.5.97, VR, GOG or other AE versions.

GPL-3.0-or-later with the Modding Exception (InterruptPackageDeadlockFix-LICENSE.txt
and InterruptPackageDeadlockFix-EXCEPTIONS.md). Source and technical notes:
https://github.com/kezc7/InterruptPackageDeadlockFix
