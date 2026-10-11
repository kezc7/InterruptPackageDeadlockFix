# In-game regression checklist

For each build before release, on Skyrim AE 1.6.1170 with SKSE 2.2.6. Keep `InterruptPackageDeadlockFix.log` from every session. The log is at `Documents\My Games\Skyrim Special Edition\SKSE\`.

## 0. Load

- [ ] `skse64.log` shows `plugin InterruptPackageDeadlockFix.dll (... 01000000) loaded correctly`.
- [ ] The plugin log shows `1.0.0 loading on Skyrim 1.6.1170.0` and both `installed at SkyrimSE+76C431` and `installed at SkyrimSE+76C62F`.
- [ ] *Negative test (optional):* on another AE runtime, the log says `not installed: only Skyrim 1.6.1170 is supported` and the game runs normally.

## 1. Followers

- [ ] Recruit a follower (vanilla, such as Lydia, and a custom one). Travel, wait and sneak with them.
- [ ] Ask them to do things (*wait here*, *follow me*, trade, pick a lock). Interrupt them mid-dialogue.
- [ ] Dismiss and re-hire. Fast travel with them.

## 2. Combat (the case where the freeze appeared)

- [ ] Fight bandits, draugr and dragons with one or more followers, for at least 20 minutes in total.
- [ ] Trigger crime alarms (steal in view of a guard, assault someone) and then yield or pay the fine.
- [ ] Let a follower get hit by the player (friendly-fire aggression), then calm them down.
- [ ] Kill enemies a follower is targeting, and loot them while the follower is still in combat.
- [ ] Watch the log for `deferred EndInterruptPackage` lines. Note the form IDs and which site (`+0x171` / `+0x36F`) they came from.

## 3. Package transitions

- [ ] NPCs with schedules (a Whiterun market day, the inn at night). Interrupt them by talking, bumping into them and drawing weapons.
- [ ] Dialogue interrupted by combat. Combat ending, then dialogue resuming.
- [ ] Brawls (Uthgerd), and guards greeting you or commenting.
- [ ] For each actor that appears in the log, check it goes back to its normal routine within a few seconds. It shouldn't stand frozen or loop.

## 4. Cell transitions and loading

- [ ] Fight right at a load door, then go through mid-combat with a follower.
- [ ] Fast travel straight after combat. Wait or sleep in an interior.
- [ ] Load a save while combat is going on. Load a save from the pause menu right after a `deferred` log line.
- [ ] Go to the main menu and load again, at least 5 times in one session.

## 5. Long play

- [ ] At least one 2-hour session with followers and regular combat. No freezes and no new crashes. Crash Logger shows nothing in this plugin.
- [ ] Check the log's deferral counts aren't growing without bound for one actor (repeated lines for the same form ID with no combat going on).
- [ ] Quit to the desktop normally. The process should exit cleanly.

## 6. Regression against the original freeze

- [ ] If you have a save that froze before (for example the 2026-10-06 and 2026-10-07 combat saves with Remiel), replay that fight several times with the plugin installed. It should not freeze. If it does, take a full memory dump and keep the log.
