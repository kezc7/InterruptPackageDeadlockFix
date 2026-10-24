Interrupt Package Probe 0.5.0 - diagnostic only, NOT a fix
Works on Skyrim AE 1.6.1170 and SE 1.5.97 (needs SKSE and Address Library)

It patches nothing. At startup it lists every call to
Actor::EndInterruptPackage in the game's code and which of them are made
while the player's extra-data read lock is held, then writes:
  Documents\My Games\Skyrim Special Edition\SKSE\InterruptPackageProbe.log

AE SELF-CHECK (Mod Organizer 2)
1. Install this archive as a mod and enable it.
2. DISABLE Interrupt Package Deadlock Fix for this run (it rewrites the
   calls this probe looks for).
3. Launch through SKSE, wait for the main menu, quit.
4. The log's last lines say "AE self-check: PASS" or "FAIL", plus a NOTE
   if there are more calls after the lock than the two the fix covers.
5. Disable or remove the probe and re-enable the fix.

Nothing needs to be loaded or played; it's safe to remove at any time.
