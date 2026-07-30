# Next steps

Handoff notes for continuing work on this fork. Written so a fresh session can
pick up without re-deriving anything.

Build and run:

```sh
./bin/build-macos.sh          # add `clean` to wipe the build dir first
./build/bin/hoverrace
```

See `README-macos.md` for the SDL2 situation, the macOS fixes already in the
tree, and how to rebuild `ObjFac1.dat` from `res/`.

---

## Done

* macOS build (real SDL2, not Homebrew's sdl2-compat shim)
* Metal renderer; high-DPI; vsync; exact-elapsed simulation timestep
* Collision fixes: reachable retry loop, wall sliding, `RAD_2_MR_ANGLE` on ARM64
* Nova craft (5th craft) with its own generated mesh, unfolds under power
* Bots that drive the circuit via room portals
* HUD: race time, lap time, lap counter, bottom-left, readable
* **Step 1** — number keys on the starting grid set the size of the field

## Remaining

### Step 2 — make bots real players

Bots are currently raw `MainCharacter`s inserted into the level. They collide
and render, but they are invisible to ranking, lap counting and results. Three
things block it:

**a. `ClientSession::GetRank()` is hardcoded to exactly four racers.**
`client/Game2/ClientSession.cpp`, ~87 lines. It pulls `players[0..3]` into
`mMainCharacter1`..`4` and compares them pairwise by hand. It needs rewriting as
a loop over an arbitrary number of racers, sorting by (laps completed, then
total time). `GetResult()` and `ResultAvaillable()` nearby have the same
assumption and feed the results table.

**b. `MAX_PLAYERS` is 4 and `players` is a fixed `std::array`.**
`client/Game2/ClientSession.h`. Raising it is not just a number: check for
split-screen and viewport assumptions before changing it. `ClientApp` decides
viewport counts from the player list, and bots must **not** get viewports.

Consider keeping human players and bots in separate collections and having the
ranking iterate over a combined view, rather than making bots occupy player
slots. That avoids disturbing split-screen entirely.

**c. Lap counting lives in Lua, not C++.**
`share/rulebooks/Race/player.lua` `on_finish_line` is what increments the lap,
tracks best lap and calls `self.player:finish()`. Bots have no `Player` and no
script peer, so they cross the line and nothing records it.

Each bot therefore needs a `Player` object and a script peer so the rulebook
lifecycle fires for it. Look at `ClientSession::AttachPlayer` / `SetPlayer` for
how a human player is wired up, and at `HoverScript/PlayerPeer` for the peer.
Bots need a name (e.g. "Bot 1") and no profile.

Verify with: 5 bots, watch for `on_finish_line` prints from bot names, and
confirm `GetRank()` returns sensible positions mid-race.

### Step 3 — end-of-race leaderboard

Depends on step 2; falls out almost free once bots rank.

`share/rulebooks/Race/player.lua` `on_finish` currently calls `hud:clear()` and
shows only "Finished in <time>". Replace with a standings table: position,
name, total time, best lap, for every racer including bots.

The data already flows through `ClientSession::GetResult(position, ...)`, which
returns name, id, laps, finish time and best lap — it is only the 4-player
assumption in (a) that makes it useless for a field of bots.

The HUD API is in `client/Game2/HoverScript/HudPeer.cpp`: `add_text`,
`add_chronometer`, `add_counter`, `clear`, and alignment constants. There is no
table widget; a leaderboard is several `add_text` lines, or a new HUD decor
class if it needs to look good.

---

## Landmines already hit

Do not rediscover these:

* **`Hud::LayoutCorner` positions only ONE element** at the corner alignment
  itself (`cornerElems.back()`). Everything else must go on the side runs, e.g.
  `SW` for the corner and `WSW` to stack upward. (The `SW`/`SSE` wiring bug is
  fixed, so `SSW` works now.)
* **`RAD_2_MR_ANGLE` was broken on ARM64** for negative angles. Fixed, but if
  anything angular misbehaves, check for the same `static_cast<unsigned>` of a
  negative double pattern elsewhere.
* **Rulebook options are read by name, not copied.** Adding a rule to
  `rulebook.lua` is not enough: `GamePeer::LStartPractice_RO` extracts each
  option explicitly. A new option is silently ignored until it is plumbed there,
  and a C++ default of 0 will clobber the rulebook's own default (use a
  sentinel).
* **`on_session_begin` never fires in this client.** `GamePeer::OnSessionStart`
  is only called from the legacy `GameApp`, not `ClientApp`. Use the rulebook or
  a session hook instead of that Lua event.
* **CMake globs sources at configure time.** After adding a new `.cpp`, re-run
  `cmake -S . -B build` or the link fails with undefined symbols.
* **`res/meshes/preview.ts` deliberately does not wrap rings.** It mirrors the
  renderer's `lU < lURes - 1` walk. Do not "fix" it to wrap; that would hide the
  seams it exists to catch.

## Verification harness

`HR_AUTO_SCREENSHOT=<n>` renders frame *n* to `~/HoverRace Screenshots/` and
exits, which makes automated visual checks possible:

```sh
HR_AUTO_SCREENSHOT=900 ./build/bin/hoverrace --exec /path/to/script.lua
magick "$HOME/HoverRace Screenshots/<file>.bmp" -alpha off PNG24:out.png
```

A startup script for testing a field of bots:

```lua
do
  local cfg = game:get_config()
  cfg:unlink()
  cfg:set_video_res(1600, 900)
end
game:on_init(function()
  game:start_practice("ClassicH", { laps = 5, bots = 5 })
end)
```

Note that digits only register once the starting grid is up; during track
loading the foreground scene is `Load` and will not consume them.
