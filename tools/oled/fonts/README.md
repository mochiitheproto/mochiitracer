# CJK pixel font of the OLED (Simplified Chinese)

`fusion-pixel-12px-zh_hans-subset.bdf` holds only the Chinese characters that
`tools/oled/textos.py` uses (83 today), cut unchanged out of **Fusion Pixel Font**, 12px,
monospaced, zh_hans, release `2026.09.25`:

- project: https://github.com/TakWolf/fusion-pixel-font (Copyright (c) 2022, TakWolf)
- file: `fusion-pixel-font-12px-monospaced-bdf-v2026.09.25.zip` from that release
  (sha256 `d75f5262f108757edb0f47ee8e3d2dfdfbecfd94558faf5ffb0c06dc5866fb3b`),
  inside it `fusion-pixel-12px-monospaced-zh_hans.bdf`
- license: SIL Open Font License 1.1, `OFL.txt` (no Reserved Font Name)

Fusion Pixel Font is built from other pixel fonts. The glyphs in this subset come from:

- **Ark Pixel Font** (Copyright (c) 2021, TakWolf, OFL 1.1, `LICENSES/ark-pixel/OFL.txt`):
  81 of them.
- **Cubic 11** (OFL 1.1, `LICENSES/cubic-11/OFL.txt`): 惑 and 紫, which Ark Pixel 12px does
  not have yet.

The release also carries the license of Galmuri, a Korean font; no glyph of it is used here.

`tools/oled/gen_assets.py` turns the subset into the `softFontCJK` bitmaps of `SoftAssets.cpp`
(each glyph in its 11x11 cell, as drawn in the font, 12 px advance). Characters are 12 px tall like
the font, centred on the line of the Latin text.

## Adding Chinese text

1. Write it in `tools/oled/textos.py`.
2. Download the release above (or a newer one) and unzip it.
3. `python3 tools/oled/cjk_subset.py <dir>/fusion-pixel-12px-monospaced-zh_hans.bdf`
   (rewrites the subset with exactly the characters `textos.py` uses; it stops if one is not in
   the font).
4. `python3 tools/oled/gen_assets.py lib/ProtoTracer/ExternalDevices/Displays`

`gen_assets.py` stops with an error naming the string when a character has no glyph (neither in
`font5x7.py` nor in this subset), so a missing character shows up there, never on the head.
