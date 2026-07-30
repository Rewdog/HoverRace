# Building and running HoverRace on macOS

Tested on macOS 26.5 (Apple Silicon, MacBookPro18,1).

## Quick start

```sh
brew install boost cmake pkg-config openal-soft freealut libxdg-basedir libyaml gettext
./bin/build-macos.sh
./build/bin/hoverrace
```

## The SDL2 situation

Homebrew's `sdl2` formula is now **sdl2-compat** — the SDL2 API reimplemented on
top of SDL3:

```
/opt/homebrew/opt/sdl2 -> ../Cellar/sdl2-compat/2.32.70
```

HoverRace does not render correctly against it, so this checkout builds against a
real SDL2 staged in `.deps/sdl2` (SDL2 2.33.0, from the SDL2 `main` branch, along
with matching `SDL2_image` and `SDL2_ttf`). `bin/build-macos.sh` puts that prefix
first on `PKG_CONFIG_PATH`; without that, pkg-config silently resolves `sdl2` to
the compat shim and you get a subtly broken build.

`.deps/` is gitignored. If it ever goes missing, rebuild it:

```sh
SDL_PREFIX="$PWD/.deps/sdl2"

git clone --branch SDL2 --depth 1 https://github.com/libsdl-org/SDL.git /tmp/SDL2
cmake -S /tmp/SDL2 -B /tmp/SDL2/build -DCMAKE_INSTALL_PREFIX="$SDL_PREFIX" \
	-DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/SDL2/build -j && \
	cmake --install /tmp/SDL2/build

for repo in SDL_image SDL_ttf; do
	git clone --branch SDL2 --depth 1 "https://github.com/libsdl-org/$repo.git" "/tmp/$repo"
	cmake -S "/tmp/$repo" -B "/tmp/$repo/build" -DCMAKE_INSTALL_PREFIX="$SDL_PREFIX" \
		-DCMAKE_PREFIX_PATH="$SDL_PREFIX" -DCMAKE_BUILD_TYPE=Release && \
		cmake --build "/tmp/$repo/build" -j && cmake --install "/tmp/$repo/build"
done
```

## macOS-specific fixes in this tree

Several things had to change to make the game work on modern macOS:

1. **Renderer selection** (`engine/Display/SDL/SdlDisplay.cpp`) — the game now
   uses the **Metal** renderer. Apple's OpenGL is deprecated and produces heavy
   vertical smearing on textures and text; Metal is clean. The renderer scan also
   honors the standard `SDL_RENDER_DRIVER` hint, so a backend can be forced
   without rebuilding:

   ```sh
   SDL_RENDER_DRIVER=opengl ./build/bin/hoverrace   # reproduce the artifacts
   SDL_RENDER_DRIVER=metal  ./build/bin/hoverrace   # the default choice
   ```

   The OpenGL artifacts have to be observed on screen — `SDL_RenderReadPixels`
   returns an all-black frame on that backend here, so `HR_AUTO_SCREENSHOT`
   captures nothing useful under `SDL_RENDER_DRIVER=opengl`.

2. **`InputStreamRwOps::RRead`** (`engine/Display/SDL/SdlDisplay.cpp`) — this
   `SDL_RWops` shim read one object per `std::istream::read()` call. SDL_image's
   macOS backend is **ImageIO**, which pulls the whole file through the callback
   in small pieces, so loading a single PNG cost hundreds of thousands of
   `istream::read()` calls and the game appeared to freeze on track load. It now
   does one bulk read and clears the stream state afterwards, so seeks performed
   during format probing still work.

3. **`SdlDisplay::Screenshot()`** — the surface format now matches the format
   passed to `SDL_RenderReadPixels` (they disagreed, which swapped color
   channels), and the capture uses the renderer's real output size.

4. **High-DPI rendering** (`engine/Display/Display.*`, `SdlDisplay.cpp`) — the
   window is created with `SDL_WINDOW_ALLOW_HIGHDPI` and a pixel scale derived
   from the real drawable size. `Display::OnDisplayConfigChanged` multiplies the
   configured resolution by it, so the legacy 3D framebuffer, font sizes and UI
   layout all follow automatically. A 1600x900 window now renders at 3200x1800.
   Mouse coordinates are scaled at the single point SDL events enter the game
   (`ClientApp::MainLoop`), since SDL reports them in logical points while the
   UI is laid out in drawable pixels.

5. **Fixed-chunk simulation timestep** (`engine/Model/GameSession.cpp`) — the
   world advanced in fixed 15 ms chunks and held back any remainder under 10 ms,
   so it advanced by a different amount than each frame covered. Positions
   drifted behind real time and caught up in a lump, which read as jitter on
   whichever object the camera was not locked to. It now simulates exactly the
   elapsed time, clamped to 100 ms so a stall can't tunnel through the level.
   Rendering is vsynced.

6. **`RAD_2_MR_ANGLE`** (`engine/Util/WorldCoordinates.h`) — converted radians
   through `unsigned int`. Casting a negative double to an unsigned type is
   undefined behaviour: x86 happened to wrap modularly, but ARM64 `fcvtzu`
   *saturates to zero*, so every negative angle collapsed to 0. That silently
   corrupted collision bounce directions (`ShapeCollisions.cpp` feeds `atan2`
   results straight in). Now routed through a signed int.

7. **Collision recovery** (`engine/MainCharacter/MainCharacter.cpp`) —
   `MINIMUM_SPLITTABLE_TIME_SLICE` was 6 while `Simulate()` only ever passed
   slices of `TIME_SLICE` = 5, so the "halve the step and retry" loop was
   unreachable and a blocked move produced no movement at all. Craft also now
   slide along walls: when every scale of a move is blocked, the translation is
   fanned out past 90 degrees to find a direction that is free.

## What else is in this fork

* **A fifth craft, the Nova** — its own mesh, generated procedurally by
  `res/meshes/gen-nova.ts` and compiled into `ObjFac1.dat`. It unfolds under
  power: each `FRAME` in a `.msh` stores complete independent geometry, so the
  wings sweep forward and the nacelles flare when the motor is on. Cycle craft
  with left/right at the start line.
* **Computer-controlled racers** (`client/Game2/BotDriver.*`) — steer through
  room portals using the level's own topology, so they work on any track.
  Configure with the `bots` rule (see below).
* **HUD** — race time, current lap time and lap counter, in the bottom-left
  corner rather than over the craft.
* **`trackdump`** (`client/TrackDump/`) — exports track geometry to JSON.
* **`hoverrace-resource-compiler`** — the original resource compiler, fixed (see
  below) so `ObjFac1.dat` can be rebuilt from `res/`.

## Bots

The number of computer-controlled racers is a rulebook rule. Edit
`share/rulebooks/Race/rulebook.lua` (plain Lua, read at startup -- no rebuild
needed):

```lua
rules = {
    laps = 5,
    bots = 3,   -- 0 disables; capped at 8
}
```

Or pass it per-session from a startup script:

```lua
game:start_practice("ClassicH", { laps = 5, bots = 5 })
```

Bots are ordinary `MainCharacter`s inserted into the level, so they collide and
render like human racers, but they are not registered as Players -- they take no
viewport and do not yet appear in the HUD or results table.

## Rebuilding game resources

`ObjFac1.dat` is compiled from the sources in `res/`. Build the compiler with
`-DHR_BUILD_UTILS=TRUE`, then:

```sh
cd res
../build/bin/hoverrace-resource-compiler ObjFac1.8bit.in ../share/ObjFac1.dat ObjFac1.def.in
```

`ResActorBuilder`'s `sfgets` threw on EOF while every parse loop terminated on
`nullptr`, so the compiler failed on *every* actor -- including the game's own
meshes. With that fixed it round-trips the shipped `ObjFac1.dat` to within 2
bytes of 1.77 MB.

Mesh sources are plain text under `res/meshes/`. Two helper tools live there:

```sh
bun run analyze.ts <file.msh>            # structure and coordinate ranges
bun run preview.ts <file.msh> [frame]    # offline render, writes a PPM
```

`preview.ts` deliberately mirrors the renderer's quad walk (`lU < lURes - 1`,
no wrap-around), so a ring only closes if the mesh repeats its first point --
wrapping in the preview would hide exactly the seams it exists to catch.

## Automated rendering checks

`HR_AUTO_SCREENSHOT=<n>` saves frame `n` to `~/HoverRace Screenshots/` and exits.
Useful for verifying rendering without a human at the keyboard:

```sh
HR_AUTO_SCREENSHOT=300 ./build/bin/hoverrace --skip-startup-warning
```

Screenshots are 32-bit BMPs; `sips` refuses them, but ImageMagick handles them:

```sh
magick "$HOME/HoverRace Screenshots/<file>.bmp" -alpha off PNG24:out.png
```

## Notes

* Logging is quieter in the default `RelWithDebInfo` build (`NDEBUG` raises the
  level). Pass `-v` for the `[info]` stream.
* The default window is 800x450. With high-DPI that renders at 1600x900;
  raising the resolution in Settings scales up from there.
* The software rasterizer is not the bottleneck -- 4x the pixels costs roughly
  6% of the frame rate -- so there is little reason to run it small.
