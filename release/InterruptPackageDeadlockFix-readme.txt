Interrupt Package Deadlock Fix 1.1.0
Skyrim AE 1.6.1170 - needs SKSE64 and Address Library for SKSE Plugins

Fixes a freeze in the vanilla engine (a deadlock), often seen with a
follower in combat.

THE FREEZE
While the game updates AI on a background thread, it holds a read lock on
the player's data. In two places it then ends an NPC's "interrupt package"
(the short behaviour an NPC switches to when something interrupts them)
without letting go of that lock. Ending the package can need to write the
player's data, and a write waits for every reader to leave - including the
thread that is waiting. The thread waits on itself, the main thread waits on
it, and the game freezes.

THE FIX
Those two calls are redirected: the interrupt package is ended a moment
later on the main thread, once the lock has been released. The NPC is kept
safely in memory until then. Nothing else in the game is changed, and the
one-frame delay can't be seen in play.

SAFETY
Each patched spot is checked byte for byte first. If it isn't exactly what
is expected (another game version, or another mod changed it), the plugin
leaves it alone and says so in its log. Nothing is written to your save, so
it can be added or removed at any time.

LOG
Documents\My Games\Skyrim Special Edition\SKSE\InterruptPackageDeadlockFix.log
- "installed at ..." lines: both spots were patched.
- "deferred EndInterruptPackage #N ..." lines: the fix stepped in (the first
  20 times, then every 100th).

INSTALL
Install with your mod manager, or copy SKSE\Plugins\InterruptPackageDeadlockFix.dll into
Skyrim's Data\SKSE\Plugins folder. Remove it the same way.

Not for Skyrim SE 1.5.97 or VR. Works alongside other freeze and crash fixes
(tested with Skyrim Freeze Fix NG, which fixes a different freeze).

Source (MIT): see the mod page.
