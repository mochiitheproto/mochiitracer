# oledsim: host simulator for the protohead's inner OLED (SSD1306 128x64 HUD)

This tool runs the **real** HUD drawing code (`HeadsUpDisplay` in `SSD1306.h/.cpp`) on x86. It
uses the real Adafruit_GFX and the **real Adafruit_SSD1306** from `.pio/libdeps`. Those
libraries send their I2C bytes to an **emulated SSD1306 controller**, and the tool
photographs what the panel physically shows. That covers invertDisplay, dim and contrast,
display on/off, segment and COM remaps, and the addressing modes.

```
./render.sh <displays_dir> [scenario.json] <out_dir> [--scale 6] [--tint white|blue|yb] [--only SUBSTR] [--title T] [--lang es|en|zh]
```

* `<displays_dir>` is a folder shaped like `lib/ProtoTracer/ExternalDevices/Displays`. It must
  contain `SSD1306.h` and `SSD1306.cpp`, and it can hold any extra `.h`, `.cpp` or `.c` files
  (icons, fonts). Every `.cpp` and `.c` file in the folder is compiled.
* The scenario defaults to `scenarios/standard.json`.
* A run rebuilds and renders in about 5 s for the 20 standard states. The common objects
  are built once (about 5 s) and cached in `build/common`.
* Nothing in the repo is ever written.
* The repo is the folder two levels up from the sim (`PROTOTRACER_REPO=<repo>` overrides it) and
  the libraries come from `$REPO/.pio/libdeps/teensy40hub75` (`OLEDSIM_LIBDEPS=<dir>` overrides it).
  The stage mirrors that repo's `lib/ProtoTracer`, so `BoopGesture.h` and `BootScreen.h` are the
  repo's.
* Boop gesture states (real `BoopGesture`, strips, bus budget): `scenarios/boop.json`, see
  "Boop gesture" below.
* `--lang en` renders the HUD's words in that language (`es`, `en`, `zh`, the numbers of
  `tools/oled/textos.py`) for every state that does not pick its own `"lang"`. Every word on the
  screen: `scenarios/textos.json` (+ `faces.json` for the face names). Side by side:
  `./sheet.py --compare out_es out_en out_zh --labels es en zh -o compare.png`.
* `OLEDSIM_DEFS="-DESPECIE_PRIMAGEN -DIDIOMA_DEFECTO=1"` adds build flags to the design and the
  driver, like PlatformIO's `build_flags` (Primagen boot text, the language of the build).

## What gets built (build.sh)

| piece | source |
|---|---|
| Print / String / avr itoa | host ports of the Teensy 4 core (`shim/Print.*`, `shim/WString.*`): same overloads, `println` = `"\r\n"` |
| Arduino.h | `shim/Arduino.h`: Teensy `wiring.h` semantics (uint32 `millis()`, the same `min/max/constrain/map`), virtual clock, `elapsedMillis`, `Serial` (silent; `OLEDSIM_SERIAL=1` sends it to stderr), Teensy PRNG, **`tempmonGetTemp()` = the state's `temp`** |
| Wire | `shim/Wire.h` + `shim/oledsim_rt.cpp`: `BUFFER_LENGTH` 136 like WireIMXRT. Bytes sent to 0x3C on `Wire` go to the controller emulator, and other addresses NACK. Each transmission advances the clock by its real bus time, about 23 ms per full frame at 400 kHz |
| Adafruit_GFX + Fonts/ | `.pio/libdeps/teensy40hub75/Adafruit GFX Library` (real) |
| Adafruit_SSD1306 | `.pio/libdeps/teensy40hub75/Adafruit SSD1306` (real, unmodified) |
| ProtoTracer bits | repo `Utils/Math`, `RGBColor`, `TimeStep`, `Effect`, `BoundingBox2D`, `DampedSpring`, `PixelGroup<2048>` (real) |
| Menu | `overlay/.../Menu/Menu.h`: the same public static API and `MenuState` enum as the real Menu, backed by plain storage the driver sets |

For each design, `build/designs/<hash>/stage/lib/ProtoTracer` mirrors the repo as symlinks.
`ExternalDevices/Displays` in that stage is a **copy of your dir**, and `Menu.h` is swapped
for the stand-in. Because of this, the usual relative includes (`"../InputDevices/Menu/Menu.h"`,
`"../../Utils/Math/Mathematics.h"`, `<Fonts/FreeSans9pt7b.h>`, `<Adafruit_SSD1306.h>`) resolve
exactly as they do in the firmware. The flags match PlatformIO's Teensy 4 C++ flags
(`-std=gnu++17 -fpermissive -fno-exceptions -fno-rtti`) plus `-DPROJECT_PROTOGEN_HUB75`, so
a design that compiles here is very likely to compile with `pio run -e teensy40hub75`.
Set `OLEDSIM_WARN=1` to see warnings from your files.

## Design contract (what the driver calls)

The driver mirrors `ProtogenHUB75Project` and `ProtogenProject` exactly:

```
HeadsUpDisplay hud(Vector2D(0,0), Vector2D(192,96));   // zero-initialised storage, like the global project
hud.SetFaceArray(faceArray, count);                   // the project's names (faces.py): DEFAULT … HAPPY
hud.Initialize();                                     // at init_ms (default 0)
loop:  // ProtogenHUB75Project::Update()
       bootActive = first BootScreen::TOTAL_MS (4050 ms) after the first loop
       if (bootActive || !Menu::UseBoopSensor()) { boopGesture.Reset(now); [hud.SetBoopGesture(STAGE_WAIT, 0, NONE)] }
       else { ev = boopGesture.Update(isBooped, now);  // the REAL BoopGesture.h, last frame's sample
              [hud.SetBoopGesture(GetStage(), min(HoldMs(now), 65535), ev)]
              if (ev == FIRE) Menu::NextFace(); }      // (+ the new face's mask, see "Boop gesture")
       // ProtogenProject::UpdateFace()
       while(!frameLimiter.IsReady()) delay(1);       // TimeStep(120)
       scenario events + boop sensor edges; if (Menu::UseBoopSensor()) isBooped = sensor;
       hud.SetEffect(Menu::GetEffect());              // no-op effect in the sim
       [hud.SetBooped(booped)]  [hud.SetFPS(fps)]     // only if your class declares them
       hud.Update();
       hud.SetFaceMax(Vector2D(192,94)); hud.SetFaceMin(Vector2D(0,0));
       hud.ApplyEffect(&mainCameraPixels);            // Engine::Render → scene effect
       render cost (render_ms, default 4; 12 for boop states)
```

* Keep the class name `HeadsUpDisplay`, the constructor `(Vector2D, Vector2D)`, and
  `SetFaceArray / SetFaceMin / SetFaceMax / Initialize / Update / SetEffect / ApplyEffect`.
* **Optional hooks**: `void SetBooped(bool)` and `void SetFPS(float)`. If your class has
  them, the driver calls them every frame with the state's `booped`/`fps`. On the real head
  the project would call them in `UpdateFace()` (for example `hud.SetBooped(isBooped)`), so
  note that one-line project change in your proposal. `index.json` lists which hooks were
  detected. To add another hook, copy the `HasSetFPS` and `callHooks` pattern in
  `src/oledsim_main.cpp`.
* **Boop hook**: `void SetBoopGesture(uint8_t stage, uint16_t holdMs, uint8_t event)`, detected
  the same way and called every loop (~83 Hz) before the frame's `hud.Update()`. See
  "Boop gesture" below.
* **Language hook**: `void SetLanguage(uint8_t)`, called right after the constructor with the
  state's `"lang"` (the head does it in its first loop, from EEPROM 201) and again when an event
  changes `"lang"` (the serial `l<N>`). Without `"lang"` it is not called (the build's
  `IDIOMA_DEFECTO`). A design without the hook ignores `"lang"` with a warning.
* **Temperature**: call `tempmonGetTemp()` (declared by Teensy's `core_pins.h`, which
  `Arduino.h` includes). In the sim it returns the state's `temp`.
* **Menu**: `Menu::GetCurrentMenu()` is 0 for normal use (a button press changes the face).
  Values 1..12 mean a setting is being edited (a press increments its value, a hold moves to
  the next menu). The value ranges are the real ones: Microphone, BoopSensor and
  SpectrumMirror take 0..1, Faces 0..19, everything else 0..9.
* The mini-face is `ApplyEffect` on the main camera's real `PixelGroup<2048>` (192x96, 64 per
  row), with colours loaded from a mask. A pixel reaches `EnableBitFaceRender` exactly as on
  the head, including the real code's `R>0 || G>0 || G>0` check, so blue-only pixels never show.

## Scenario JSON

```jsonc
{
  "init_ms": 0, "loop_hz": 120, "render_ms": 4,      // optional timeline knobs
  "defaults": { ...any state field... },              // merged under every state ("values" merged per key)
  "states": [
    {
      "name": "s06_edit_brillo_3",
      "millis": 10000,          // the frame visible at this time (ms since boot)
      "menu": "Bright",         // 0..12 or MenuState name (Faces, Bright, AccentBright, Microphone, MicLevel,
                                //   BoopSensor, SpectrumMirror, FaceSize, Color, HueF, HueB, EffectS, FanSpeed)
      "face": "DEFAULT",        // name, or its number in faces.py
      "values": {"Bright": 3},  // or a list of 13; a bare "Bright": 3 in the state works too
      "temp": 46.0,             // tempmonGetTemp()
      "lang": "en",             // hud.SetLanguage(): "es", "en", "zh" or 0..2 (render.py --lang = default)
      "booped": false, "fps": 60,
      "mask": "auto",           // "auto" (captured: DEFAULT, DEAD, AMOR, OWO, HAPPY), "none", or a .txt path
      "phases": true,           // write _a (frame at millis) + _b (next DIFFERENT frame, else = _a)
      "phase_window": 1500,     // ms searched for _b
      "gif": true, "gif_ms": 2000,   // animated GIF of every change in [millis, millis+gif_ms]
      "events": [ {"t": 8500, "face": "AMOR", "temp": 75} ]   // field changes mid-run (any state field)
    }
  ]
}
```

Boop fields (see "Boop gesture"): `"boop"` (preset name or `[[t_ms, 0|1], ...]`), `"boop_at"`,
`"strip"`, `"strip_cols"` (default 4), `"render_ms"` (per state), `"fire_mask"`
(`"auto"` | `"follow"` | `"keep"`).

A top-level JSON list of states also works. Mask files hold 32 rows of 64 `RRGGBB` hex values,
with row 0 at the top. This is the `DumpFrame()` format, the same as `cap-caras/*.txt` and the
hostrender `out/*.txt`. A mask path is looked up as given, then relative to the scenario file,
then in `masks/`.

Every state runs in its own `fork()`ed process from t=0, so statics, splash timers and
`TimeStep`s are fresh. A state with `"millis": 10000` really simulates 10 s of loop. Time
is virtual, so this takes milliseconds.

### Standard state set (`scenarios/standard.json`)

The settings are the head's real values: mic OFF (0), boop ON, Color CYAN (2), FaceSize 9,
HueF 5 and HueB 2, fans 9, no effect, Bright 3, sides 5, temp 46 °C.

`s01_idle_default` (cara00), `s02_idle_dead` (cara16), `s03_idle_amor` (cara17),
`s04_idle_kaomoji` (image slot, no mask), `s05_idle_kp140` (image slot with a label, no mask),
`s06_edit_brillo_3`, `s07_edit_abanicos_9`, `s08_edit_micro_off`, `s09_edit_boop_on`,
`s10_edit_color_cyan`, `s11_edit_efecto_glitch`, `s12_edit_tono_7`, `s13_edit_lados_5`,
`s14_edit_tamano_9`, `s15_splash_t1000`, `s16_splash_t3500`, plus the temperature states
`s17_temp_60`, `s18_temp_72` and `s19_temp_85` (GIFs), and `s20_boot` (GIF of the first 6 s).

Every state except s20 produces `<name>_a` and `<name>_b`, so the file names are identical
for every design. The contact sheet collapses a static pair into one tile marked
"static (a = b)". A pair that differs is tagged in orange as "blink/anim".

## Outputs (`<out_dir>`)

* `<img>.png`: what the eye sees. Scale is 6x, each pixel is a dot with a 1 px gap plus a
  soft bloom, lit pixels are cool white (`--tint blue` gives a blue module, `yb` a
  yellow/blue two-colour module), and the background is near-black. Contrast is applied:
  0xCF (the Adafruit init value) is full brightness and `dim(true)` (0x00) is about 45%.
* `<img>_1x.png`: exact 128x64 (lit = panel colour, unlit = (7,9,12)).
* `<state>.gif`, `index.json` (per image: t_ms, shown_ms = end of the last `display()`,
  contrast, invert, lit_pixels, static, hooks, controller warnings; plus a per-state `states`
  list with the boop gesture log, see below), `contact_sheet.png`, and `raw/` (plan, raw 0/1
  frames, and `<state>.boop` = gesture/loop/bus log).
* `<state>_strip.png` for boop states with `"strip": true`.

Contact sheets:

```
./sheet.py <out_dir> [-o x.png] [--cols 4] [--scale 3]                       # one render
./sheet.py --compare outA outB outC --labels A B C -o compare.png [--scale 2] # side by side, row per image
```

## Boop gesture ("boop + boooop" = next face)

The driver runs the **real** `lib/ProtoTracer/Examples/Protogen/BoopGesture.h` (from the stage
tree, so whatever the repo has) in the same order as `ProtogenHUB75Project::Update()`:
gesture first with **last frame's** `isBooped`, then the hook, then `Menu::NextFace()` on FIRE,
then `UpdateFace()` samples the sensor and calls `hud.Update()`. During the boot (the first
`BootScreen::TOTAL_MS` = 4050 ms after the first loop) and while `BoopSensor` is 0 the gesture is
`Reset()` every frame and the hook gets `(STAGE_WAIT, 0, NONE)`, like the head. With
`BoopSensor` 0 the sensor sample is also frozen (the head only reads the APDS when it is on).

### Design hook

```cpp
#include "../../Examples/Protogen/BoopGesture.h"   // enum names (Arduino-free)
void SetBoopGesture(uint8_t stage, uint16_t holdMs, uint8_t event);
// stage = BoopGesture::Stage (STAGE_WAIT, STAGE_ARMED, STAGE_TAP, STAGE_GAP, STAGE_HOLD)
// holdMs = ms held in STAGE_HOLD (else 0), event = this frame's Update() result (NONE on most frames)
```

It is called **every loop** (~83 Hz), but `hud.Update()` only draws when its own TimeStep says so
(5 Hz today), so the design must **latch** events itself (keep the last notable event +
`millis()`). `tests/boop_demo/` is a minimal self-test design that does this (hold bar, 100 ms
refresh during a gesture): `./render.sh tests/boop_demo tests/boop_demo.json /tmp/x`.

### Scenario fields

* `"boop"`: a preset name from **`boop_presets.json`** (the one table of patterns, shared with
  the firmware's serial `g<N>`, see "Same presets on the head" below) or an explicit list of
  sensor edges `[[t_ms, 0|1], ...]` (absolute ms; the raw `IsBooped()` becomes that value at that
  time).
* `"boop_at"`: start of a preset (ms). Default `millis - 500`; for an explicit list, its first
  rising edge.
* Presets (durations in ms, ON first): `doble` on 150 / off 150 / on 1200; `sencillo` on 150;
  `doble_tap` on 150 / off 150 / on 200; `suelta` on 150 / off 150 / on 500; `abrazo` on 2000;
  `seguido` = `doble`, 1000 off, `doble` again.
* `"strip": true`: writes `<state>_strip.png`, every **distinct** frame visible in
  `[boop_at-200, boop_at+gif_ms]` at 6x, labelled with the time relative to `boop_at` (when the
  picture appeared = end of the transfer that changed it), the latest gesture event (and when),
  the stage (+ ms held) and the contrast. `"strip_cols"` sets the wrap (default 4).
* A boop state with `gif`/`strip` records from `boop_at-200` to `boop_at+gif_ms` (and at least
  to `millis` + `phase_window` with `phases`); the GIF covers that window with real timing. The main image (`<state>.png`, or `_a`) is still the frame
  visible at `millis` (`scenarios/boop.json` uses `millis` 10700 = mid-hold of `doble`).
* Loop timing: boop states default to `render_ms` **12** (a frame without OLED traffic takes
  ~12 ms like the head, ~83 FPS; a frame that sends a full OLED frame takes ~36 ms in the sim,
  ~39 ms measured on the head). A state (or `defaults`) can set its own `"render_ms"`.
  Non-boop states keep the scenario's `render_ms` (default 4), so old renders do not change.
* On FIRE the stand-in `Menu::NextFace()` runs (`face = (face+1) % faces`) and, if the state's mask
  is `"auto"` (or `"fire_mask": "follow"`), the new face's mask is loaded in that same frame
  (as `SelectFace()` does): captures for DEFAULT and DEAD/AMOR/OWO/HAPPY, `scenarios/ref_caraNN.txt`
  for ANGRY…SAD and TACHA,
  none for the rest. An explicit mask path stays put (`"fire_mask": "keep"` is the default for it).

### What `index.json` gets (`states[]`, one entry per state; `boop` only for boop states or when
the gesture produced events)

* `events`: `[{t_ms, rel_ms, name, dur_ms, code}]` (names = `BoopGesture::Name()`: `tap1`, `hold`,
  `FIRE`, `boop-sencillo`, `cancel:solto`, `cancel:tap-largo`, ...), `face_changes`, `stages`
  (every stage change), `sensor_samples` (the `isBooped` the head would read), and for presets
  `expected_events` / `events_as_expected` (from `boop_presets.json`; nothing when BoopSensor is 0).
* `oled_transfers`: every loop that talked to the panel during the whole run:
  `{t_ms (first byte), bus_ms, data_bytes, cmd_bytes, bursts (display() calls), delay_ms
  (clock spent in the design that was not I2C, i.e. delay())}`.
* `window` = `[boop_at, last FIRE/cancel + 1500 ms]` (the "active gesture" span of the bus
  budget): `typical_frame_ms`, `max_frame_gap_ms`, `max_frame_gap_active_ms` (only while the
  gesture is in TAP/GAP/HOLD; > 100 ms = `CANCEL_STALL` on the head), `max_frame_ms_with_oled`,
  `oled_frames`, `oled_frames_per_s`, `min_oled_interval_ms`, `oled_cmd_only`,
  `loops_with_2plus_displays`, `hud_delay_ms_max`, and `budget_ok` / `budget_problems`
  (frames < 100 ms apart, 2+ `display()` in one loop, any `delay()`, a mid-gesture gap > 100 ms).
* `render.py` also prints one line per boop state: event sequence with times, OLED frames in
  the window and the max frame gap.

Note: the design's own bus traffic moves the gesture's sampling grid, so event times shift by
a frame or two between designs (e.g. FIRE at +1121 ms with today's design, +1144 ms with the
10 Hz demo). That is real head behaviour, not noise.

### Same presets on the head (`g<N>`)

`boop_presets.json` is the ONE table: each preset has its `g` number, and
`./gen_boop_presets.py` turns it into `lib/ProtoTracer/Examples/Protogen/BoopPresets.h`, which
the firmware's `BoopScript.h` replays for the serial command `g<N>` (1 doble, 2 sencillo,
3 doble_tap, 4 suelta, 5 abrazo, 6 seguido, 0 stop): 500 ms not booped, the pattern, 1 s not
booped, then it ends by itself. It only replaces what `BoopGesture::Update()` sees
(`SelectFace` keeps the real sensor). Edit the JSON, run the generator, rebuild.

```
./gen_boop_presets.py              # rewrite BoopPresets.h from the JSON (--check: exit 1 if stale)
tests/boop_inject.sh               # host test: header up to date + every preset through the REAL
                                   # BoopGesture.h via BoopScript.h at 12 ms frames and at 12 ms with a
                                   # 39 ms frame every 5th (all phases) = the JSON's "expect", once
tests/fake_teensy/captura_test.sh  # end to end: ../captura.py --gesto against a fake head on a pty
                                   # (+ check_idioma.py: l<N> = OK/ERR, lang= in the status, the OLED's words)
```

`tests/fake_teensy/` builds a real-time fake head: the design's HUD (this simulator's build), the
real `BoopGesture.h`/`BoopScript.h`, and `ReadSerialCommands()`, `RunCommand()`, `DumpOled()` and
the gesture block of `Update()` copied verbatim from `ProtogenHUB75Project.h` at build time. It
prints its pty (`fake_teensy <sim_dir> [--face N] [--boop-off] [--temp C] [--secs S]`), so
`captura.py --puerto <pty>` talks to it like to the Teensy. The test checks that every frame
`captura.py --gesto` saved is a frame the simulator also draws for that preset, and that the key
pictures (1st boop caught, the check bar) are among them.

On the head (no finger needed): `tools/captura.py --gesto 1` (needs the boop sensor ON in the menu).

### Peripheral-vision check

```
./glance.py <out_dir> <boop_state> [--at=-100,+300,...] [-o x.png]
```

Each picked frame at 1x, blurred 1 px and 2 px (brightness follows the contrast), plus how much
of each change survives the 2 px blur (mean |diff| x100). The wearer reads this screen out of the
corner of their eye through the helmet: a step that only changes 1 px details scores near 0.

### Regression check

```
./same.py <dirA> <dirB> [--strict] [--only SUBSTR]
```

Compares every `*_1x.png` pixel by pixel and, when both have `raw/`, every state's whole frame
sequence (what the GIF is made of: pixels, contrast, on/off). `--strict` also requires the same
frame times and the same OLED transfers (bus traffic). Exit 0 = all identical. A design that adds
the boop icon must stay identical on `standard.json` and `mochii.json` (no boop in them):

```
./render.sh <current Displays> scenarios/standard.json /tmp/ref_std
./render.sh <new Displays>     scenarios/standard.json /tmp/new_std
./same.py /tmp/ref_std /tmp/new_std --strict
```

## What the sim showed in stock ProtoTracer's HUD (before the redesign; real behaviour, not sim artefacts)

* At boot, `Initialize()` clears the buffer, sets `invertDisplay(true)` and calls `display()`,
  so the whole panel is **lit white** until the first splash about 200 ms later (`s20_boot.gif`).
* Splash 1 runs from about 0.2 to 2.7 s and splash 2 from about 2.7 to 5.3 s. The grid
  appears at about 5.3 s.
* `CheckInvertPrintText` blinks whichever cell matches `GetCurrentMenu()`. In normal use the
  menu is 0, which is also the FACE cell, so **the face name blinks all the time while idle**.
* Values are shown as `percentArray[v]`, which means (v+1)·10%: Bright 3 reads "40%".
* The mini-face is the main camera squeezed into 59x26 and **mirrored horizontally**. It
  looks like the left (mirror) half of the `[espejo | normal]` capture PNGs.

## Caveats

* Hardware scroll, fade/blink, zoom, non-default COM pin config, display offset and start
  line are only partly emulated or not at all. They show up as `warnings` in `index.json` and
  in red on the sheet.
* Brightness versus contrast is an approximation. The real panel's colour and gamma depend on
  the module.
* The mini-face mask is a fixed capture, so the face does not animate (no blink or
  wiggle). The menu effect (`EffectS`) is a no-op in the sim, so GLITCHX does not distort
  the mini-face.
* The firmware also calls `ApplyEffect` on the two 88-pixel Delta side cameras (local coords
  x 12..76, y 13..90, inside the mini-face mapping). The sim only feeds the main 64x32 camera.
  `ProtogenHUB75Project` aligns no object in the side cameras' view (X ≥ 204), so they should
  render black.
* The face names are the firmware's `faceArray`, copied in `faces.py`, `src/oledsim_main.cpp`
  and `tests/fake_teensy/fake_teensy.cpp`: when the faces change, change the three. Scenarios name
  faces by name, so they survive a renumbering.
* Boop: while booped, the head's `SelectFace()` swaps faces 0-5 for Surprised and DEAD/AMOR/OWO/HAPPY
  for TACHA on the LEDs. The sim keeps the state's mask, so the mini-face does not show that
  swap. The APDS read is not on the simulated bus (its cost is inside the 12 ms frame).
* `unsigned long` is 64-bit on x86 (32-bit on Teensy). `millis()`/`micros()` return
  `uint32_t` like Teensy, but other `long` arithmetic in a design could differ when it
  overflows.

## Layout

```
render.sh / render.py   build + scenario → plan → run → PNG/GIF/index/sheet
boopstrip.py            boop log parsing, window/bus-budget summary, strip PNG
faces.py                the firmware's face names (index = face number) + which faces have a mask
boop_presets.json       THE boop pattern table (doble, sencillo, doble_tap, suelta, abrazo, seguido; g = g<N>)
gen_boop_presets.py     boop_presets.json -> lib/ProtoTracer/Examples/Protogen/BoopPresets.h (firmware g<N>)
glance.py               peripheral-vision check of a boop state (1x, 1 px and 2 px blur)
same.py                 regression check between two renders (pixels, sequences, --strict bus)
build.sh                common objects (cached) + per-design stage/link → prints the binary path
sheet.py                contact sheet / --compare
oledlook.py             frame parsing + OLED look rendering
src/oledsim_main.cpp    driver (plan reader, fork per state, project-faithful loop, hooks)
shim/                   Arduino/Teensy core host ports, Wire (+SSD1306 emulator in oledsim_rt.cpp), SPI stub
overlay/                Menu.h stand-in
masks/                  captured LED frames (cara00/16/17/18/19 × 3 frames; NN = the slot when captured)
scenarios/standard.json standard state set
scenarios/faces.json    every face at idle + the hot light over the live ones
scenarios/boop.json     boop states b01..b13 (gif + strip each)
scenarios/mochii.json   extra states s17-s22 (temperature, BSOD) + edge cases x01-x14
scenarios/minmax.json   every menu at its min and max value
scenarios/textos.json   every word of the HUD: boot, settings, both sides of the switches, all colour /
                        hue / effect names, a language change mid-run (render with --lang es|en|zh)
tests/hooks_demo/       self-test design (hooks, FreeSans font, extra .cpp, no invert, dim, events)
                        → ./render.sh tests/hooks_demo tests/hooks_demo.json /tmp/x
tests/boop_demo/        self-test design for SetBoopGesture (latching, hold bar, 10 Hz in a gesture)
                        → ./render.sh tests/boop_demo tests/boop_demo.json /tmp/x
tests/boop_inject.sh    host test of the g<N> fake finger (BoopScript.h + the real BoopGesture.h)
tests/fake_teensy/      real-time fake head on a pty + captura.py --gesto end-to-end test (+ l<N> language)
```
