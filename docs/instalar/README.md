# Web installer

Static page that flashes MochiiTracer · Protogen OS onto a Teensy 4.0 from Chrome or
Edge (WebHID + WebSerial). It has no build step: GitHub Pages serves this folder as-is
at `/instalar/`. The interface is in Spanish, English and Simplified Chinese; it
picks the browser language and can be forced with `?lang=es|en|zh`. The species
defaults to Protogen (or the last one picked) and can be forced with
`?species=protogen|primagen`. `#console` opens the serial console section.

| File | What it is |
|---|---|
| `index.html` | Page markup (English fallback text, replaced by `i18n.js`) |
| `uploader.js` | Release lookup, Intel HEX parsing, WebHID flashing, WebSerial reboot and console |
| `i18n.js` | Every interface string, in `es`, `en` and `zh` |
| `visor.js` | LED visor preview; faces are real 64x32 frames from the ProtoTracer engine |
| `style.css` | Styles (light and dark) |

Adapted from coelacant1's uploader in ProtoTracer
(`.docs/prototracer-firmware-uploader.html` / `.js`). GNU AGPL-3.0.

## Where the firmware comes from

Each release has six builds, one per species and default language:

```
mochiitracer-teensy40-protogen-es.hex    mochiitracer-teensy40-primagen-es.hex
mochiitracer-teensy40-protogen-en.hex    mochiitracer-teensy40-primagen-en.hex
mochiitracer-teensy40-protogen-zh.hex    mochiitracer-teensy40-primagen-zh.hex
```

They are the same firmware built with different flags: `-D IDIOMA_DEFECTO=0|1|2`
(Spanish, English, Simplified Chinese) and, for Primagen, `-D ESPECIE_PRIMAGEN`. The
species only changes the text the head shows ("PRIMAGEN OS" at boot, the blue
screen...); the electronics are the same. The language is just the default: the head
can switch it at runtime with the `l<N>` serial command, and remembers that choice in
EEPROM even across installs, until `l255` sends it back to the language of the `.hex`.

The page lists the releases of `mochiitheproto/mochiitracer` through the GitHub API
(unauthenticated: 60 requests per hour per IP) and picks the asset for the chosen
species and language. It selects the newest release that isn't a pre-release (or the
newest one, if all are), marks it "latest" and labels every pre-release as such, the
selected one included.

A web page can't download release assets straight from GitHub. Both
`browser_download_url` and the API asset endpoint redirect to
`release-assets.githubusercontent.com`, which sends no `Access-Control-Allow-Origin`
header, so `fetch()` fails (checked in Chrome on 2026-10-04; the upstream uploader
has the same problem). So, in order:

1. **Same-origin copy:** `firmware/<tag>/<asset>` next to this page.
2. **API asset endpoint** with `Accept: application/octet-stream`. Fails today; kept
   in case GitHub adds CORS headers.
3. **Manual:** the page links the asset; the user downloads it and drops it in the
   ".hex file" box.

If the GitHub API can't be reached (offline, rate limit, repo still private), the page
uses `firmware/manifest.json` as the list of releases.

### The `firmware/` folder

A release build (the six species × language variants, built with the `IDIOMA_DEFECTO` and
`ESPECIE_PRIMAGEN` flags described in the main README) is a folder with the six `.hex` files,
`manifest.json`, the licenses of the pixel font built into the firmware (`OFL-1.1-*.txt`), the
notices of the libraries compiled into it (`THIRD-PARTY-NOTICES.txt`, `LGPL-2.1.txt`) and
`SHA256SUMS` (of the `.hex` files and the licenses). To
publish a release on the page, copy that folder to `firmware/<tag>/` and its
`manifest.json` to `firmware/manifest.json`:

```
docs/instalar/firmware/
├── manifest.json                       ← list of releases the page reads
└── v1.0.0/
    ├── mochiitracer-teensy40-protogen-es.hex
    ├── …                               (the other five)
    ├── manifest.json                   (same file, harmless)
    ├── OFL-1.1-fusion-pixel-font.txt   (+ ark-pixel and cubic-11: where its glyphs come from)
    ├── THIRD-PARTY-NOTICES.txt         (+ LGPL-2.1.txt: libraries compiled into the .hex)
    └── SHA256SUMS
```

`manifest.json` has the same shape as the GitHub releases API, plus a few fields per
file. The page uses `tag_name`, `prerelease`, `published_at`, `html_url` and each
asset's `name` and `sha256`; the rest is for people and scripts.

```json
{
  "schema": 1,
  "repo": "mochiitheproto/mochiitracer",
  "releases": [
    {
      "tag_name": "v1.0.0",
      "name": "MochiiTracer v1.0.0",
      "version": "1.0.0",
      "prerelease": false,
      "published_at": "2026-10-04T08:00:00Z",
      "html_url": "https://github.com/mochiitheproto/mochiitracer/releases/tag/v1.0.0",
      "board": "teensy40",
      "assets": [
        {
          "name": "mochiitracer-teensy40-protogen-es.hex",
          "species": "protogen",
          "lang": "es",
          "lang_index": 0,
          "size": 1234567,
          "lines": 33359,
          "sha256": "…64 hex digits…"
        }
      ]
    }
  ]
}
```

Newest release first. `lines` is the number of lines in the `.hex`; the build refuses
anything over 65,536, which is what `teensy_loader_cli` can read.

**Checksums:** when an asset has a SHA-256 (`sha256` in `manifest.json`, or the
`digest` field the GitHub API now returns), the page checks the downloaded file
against it and refuses to use a copy that doesn't match. When the API is reachable,
the SHA-256 for each asset comes from the API or, if the API has none, from
`firmware/manifest.json`.

## Restarting into the bootloader without the button

The page opens the Teensy's USB serial port at **134 baud** and closes it. The Teensy 4
core reboots into the bootloader when the host sets that line coding:

- `cores/teensy4/usb.c` (Teensyduino 1.160): `endpoint0_complete()` copies the
  `CDC_SET_LINE_CODING` request (0x2021) and, if the baud rate is 134, starts the SOF
  interrupt and sets `usb_reboot_timer = 80`.
- The SOF handler in `usb_isr()` counts it down and calls `_reboot_Teensyduino_()`,
  which runs `asm("bkpt #251")`: the bootloader chip takes over.
- `CDC_STATUS_INTERFACE` exists in the default `USB_SERIAL` type (`usb_desc.h`), the
  one the PlatformIO build uses.

It needs the running firmware's USB stack to be alive. If the firmware is frozen, the
Teensy's pushbutton always works.

## Serial commands the console sends

One command per line, ending in `\n`; the firmware ignores lines that don't match
exactly, and lines longer than 7 characters.

| Command | Does |
|---|---|
| `s` | Status line (`S face=… bright=… temp=… lang=<N>`) |
| `b0` … `b9` | Brightness |
| `f<N>` | Face N |
| `n` | Next face |
| `l0` / `l1` / `l2` | Head language es / en / zh, kept in EEPROM (firmware v1 and newer; keep `HEAD_LANG_CMD` in `uploader.js` in sync) |
| `l255` | Forget that choice: back to the language the installed `.hex` was built with ("Language of the .hex" button) |

## Changes from the upstream uploader

- Releases come from this fork, one asset per species and language, with a version
  picker, and downloads are checked against the release's SHA-256.
- **Fix:** the parser returned `blocks.filter(b => b)`, which compacts the block list.
  `flashFirmware()` computes each address from the index, so every block after a gap
  in the HEX went to the wrong address. Now the list stays sparse. (Current builds
  have no gaps, so the reports sent for them are byte-identical to upstream's.)
- **Fix:** HEX lines with non-hex characters passed the checksum (`parseInt` gives
  `NaN`, and `NaN & 0xFF` is 0). They're rejected now.
- **Fix:** a refused report is retried for up to 20 s instead of 5 times 100 ms apart.
  The first block starts an erase that HalfKay finishes in the background, refusing
  reports (`NotAllowedError`) until it's done, and the erase takes longer the bigger
  the old firmware was. Measured on a Teensy 4.0 over Linux: the third report was
  refused for longer than the 5 tries over a 550 KB firmware, so the install stopped at
  block 5. `teensy_loader_cli` also retries for seconds.
- The image is checked before erasing: FlexSPI `FCFB` tag (Teensy 4), board from the
  flash size at offset 0x50, size limit. The file name no longer has to contain
  `teensy40` (a PlatformIO `firmware.hex` was rejected before).
- Restart into the bootloader over WebSerial, and auto-detect the bootloader once
  the browser has permission.
- The serial console waits for the read pipe before `close()` (upstream didn't, so
  the port could stay locked), sends commands and parses the status line.

## Trying it locally

WebHID and WebSerial need a secure context; `localhost` counts.

```sh
cd docs && python3 -m http.server 8000
# open http://localhost:8000/instalar/
```
