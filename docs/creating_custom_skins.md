# Creating a Custom Skin

The player's entire look — window art, buttons, LEDs, sliders — comes
from a single skin file (`.ays`) loaded at startup or swapped in live
via **Tools → Change Skin**. This document explains the file format
and the exact pixel layout a skin bitmap must follow, so you can build
your own.

This is a from-scratch reference for the C11 port. It doesn't assume
you have the original Pascal `ay_emul` source open, though the pixel
coordinates below were transcribed directly from it
(`MainWin.pas:3787-3820`) and are exercised by `gui/src/mainwin.c`,
`gui/src/zones.c`, and `gui/src/skin.c` in this codebase.

## What a skin controls (and what it doesn't)

A skin supplies the **pixel art** for the main player window: the
background, every button's normal/pressed appearance, the two LEDs,
and the volume/progress slider handles.

A skin does **not** control the window's *shape*. The player window is
an irregular, rounded silhouette (not a plain rectangle), and that
silhouette is a fixed table baked into the app (`gui/src/regions.c`,
ported from the original's static `rgn.inc`) — it is not derived from
the skin bitmap and can't be changed by one. Every skin must fit the
same 358×123-pixel window shape; only what's *drawn inside* it changes.

The About-box bitmap (`gui/assets/about.bmp`) is a separate, fixed
resource baked into the application. It is not part of the skin and
is not swappable.

## The skin canvas

A skin bitmap is **358×150 pixels**, a standard uncompressed 24-bit
BMP. The visible window only shows the top 358×123 region at runtime;
the remaining 27 rows (123–150) are a sprite sheet holding the
*pressed*/*on* states for buttons, LEDs, and slider handles that don't
fit conveniently in the visible layout. Think of the whole canvas as
one texture atlas, not a single flat picture.

Open `gui/assets/default_skin.ays` (see [Packaging](#packaging-your-skin-into-an-ays-file)
for how to unpack it) in an image editor to see a working example
before you start.

## Element coordinates

All coordinates are `(x, y)` pixel offsets into the 358×150 canvas,
top-left origin. "Dest" is where the element is drawn on screen
(always inside the visible 358×123 area); "Source (normal)" and
"Source (pressed)" are same-sized rectangles elsewhere on the canvas
that get copied to that destination depending on state. Buttons and
LEDs are drawn as a **plain, opaque rectangle copy** — no
transparency — so every source rectangle must be fully painted, edge
to edge.

### Buttons

| Button        | Dest (x,y,w,h)     | Source, normal | Source, pressed |
|---------------|--------------------:|----------------|------------------|
| Play          | 119, 96, 35, 27     | 119, 96        | 119, 122         |
| Pause         | 158, 96, 35, 27     | 158, 96        | 158, 122         |
| Stop          | 197, 96, 35, 27     | 197, 96        | 197, 122         |
| Open          | 275, 96, 35, 27     | 275, 96        | 275, 122         |
| Previous      | 80, 96, 35, 27      | 80, 96         | 80, 122          |
| Next          | 235, 96, 35, 27     | 235, 96        | 235, 122         |
| Loop          | 52, 100, 21, 21     | 52, 100        | 337, 103         |
| Playlist      | 310, 77, 26, 26     | 310, 77        | 26, 124          |
| Mixer         | 318, 21, 26, 26     | 318, 21        | 52, 124          |
| Tools         | 322, 50, 26, 26     | 322, 50        | 0, 124           |
| About (text)  | 258, 84, 49, 8      | 258, 84        | 0, 115           |
| Minimize      | 282, 6, 16, 16      | 282, 6         | 0, 0             |
| Close         | 304, 6, 16, 16      | 304, 6         | 342, 0           |

Play/Pause/Stop/Open/Previous/Next all share the same simple pattern:
the pressed sprite is directly below the normal one, 26px down. The
other buttons pull their pressed sprite from elsewhere in the lower
sprite-sheet band (123–150) because their normal-state art sits too
close to the canvas edge to fit a same-size sprite directly beneath it.

Minimize's pressed sprite is pinned to the canvas's very top-left
corner (0,0)-(16,16) — this is the original's own design (`MainWin.pas:
3807-3808` passes literal `0, 0`), not a port gap. Decide deliberately
what lives in that corner; the default skin leaves it a plain area, so
pressing Minimize reads as a subtle flash rather than a designed icon
swap.

### LEDs (AY / YM / Stereo chip-type indicators)

| LED     | Dest (x,y,w,h)     | Source, off | Source, on |
|---------|--------------------:|-------------|------------|
| AY      | 99, 26, 45, 7       | 99, 26      | 312, 142   |
| YM      | 144, 26, 46, 7      | 144, 26     | 311, 134   |
| Stereo  | 190, 26, 44, 7      | 190, 26     | 313, 126   |

The "off" sprite is just the LED's own resting appearance in the base
window art (no separate copy needed elsewhere). "On" sprites are
packed into the bottom-right corner of the sprite-sheet band, stacked
upward.

Exactly one of AY/YM lights at a time depending on the loaded file's
chip type; Stereo is always lit (this port's audio output is always
stereo).

### Sliders (volume and progress)

Slider tracks are drawn straight from the base skin art (no code-side
overlay); only the draggable **handle** is a separate sprite,
composited on top wherever the slider's current value places it.

| Slider    | Track (x,y,w,h)     | Handle size | Handle source |
|-----------|----------------------:|-------------|---------------|
| Volume    | 237, 22, 70, 12       | 18×11       | 317, 113      |
| Progress  | 96, 83, 159, 10       | 20×10       | 0, 103        |

**Handles use color-key transparency**, not a plain rectangle copy:
whatever color sits in the handle sprite's own top-left pixel (e.g.
`(317,113)` for the volume handle) is treated as transparent
everywhere it appears within that sprite — this is how the original's
rounded/angled handle shapes (a wedge for volume, a rounded pill for
progress) show the track underneath instead of a hard box. Pick a
color for the handle's corner pixels that doesn't appear anywhere else
inside the handle art itself, or you'll get unwanted holes. (Unless
you're using a real alpha channel for the handle - see below - in
which case the color key is ignored entirely and this doesn't apply.)

### Alpha channel support (C11-only, backwards-compatible)

The original Pascal player has no real alpha channel anywhere — the
handle "transparency" above is the only trick it has, a single fixed
color treated as a hole. This C11 port adds real per-pixel alpha on
top of that, entirely opt-in:

- Save your skin bitmap as a **32-bit BMP with an alpha channel**
  (e.g. ImageMagick: `convert art.png -type TrueColorAlpha -define
  bmp:format=bmp4 skin.bmp`) instead of the classic 24-bit BMP.
- Every button, LED, and the whole background composites through
  Cairo already, so soft/antialiased edges anywhere in the skin just
  work with no other change.
- For the slider handles specifically (the one place with hand-rolled
  compositing, `gui/src/zones.c`'s `draw_pixbuf_region_keyed`): if the
  loaded bitmap has a real alpha channel, that per-pixel alpha is used
  directly and the color-key rule above is skipped entirely - you can
  paint a properly antialiased handle edge instead of a hard
  color-keyed silhouette.

**This is fully backwards compatible.** A plain 24-bit BMP (what every
existing skin, including the bundled default, already is) has no
alpha channel at all, so `gdk_pixbuf_get_has_alpha()` reports false
and every code path takes the exact same route it always has -
old skins are pixel-for-pixel unaffected. There is nothing to migrate
and no version flag; the loader simply looks at what the bitmap
already declares.

One real limitation carries over from [Packaging](#packaging-your-skin-into-an-ays-file)
below: this only helps once you can actually produce the `.ays`
container in the first place, since that still requires an external
LZH-5 encoder or the original tool.

### Song-title ticker

The scrolling song-title text is drawn by the application directly —
there's no ticker sprite to supply. It occupies a fixed rect:

| Element | Rect (x,y,w,h)       |
|---------|------------------------|
| Ticker  | 108, 48, 197, 24        |

Text is composited onto whatever's *already in the skin's base art* at
that rect using a bitwise AND against a gray text mask, the same
technique the original uses: the skin's own background shows through
untouched except where letters darken it. In practice this means
you should paint that rect as a plain, light, LCD-style backdrop (the
default skin uses white) — dark art there will make the gray title
text hard to read, and very busy art will fight visually with the
scrolling letters.

### Visualizer (spectrum / amplitude bars)

| Element   | Rect (x,y,w,h)     |
|-----------|----------------------:|
| Spectrum  | 26, 34, 65, 20        |
| Amplitude | 50, 18, 17, 15        |

Like the ticker, these are drawn programmatically (solid dark-gray
bars, `#464646`), not sourced from the skin bitmap. Leave a visually
appropriate backdrop in the skin art under these rects — a dark
recessed "meter" look reads best — but no specific pixel content is
required there.

### Title-bar drag zone

Clicking and dragging anywhere in this rect moves the window. It's a
behavioral zone, not a sprite — no art requirement beyond whatever's
already part of your window background at that location.

| Element    | Rect (x,y,w,h)     |
|------------|----------------------:|
| Drag zone  | 84, 5, 195, 17         |

## Packaging your skin into an `.ays` file

An `.ays` file has three parts, in order:

1. **24-byte magic header**: the literal ASCII bytes
   `Ay_Emul 2.0 Skin File\r\n\x1a` (21 visible characters + CR/LF +
   0x1A, 24 bytes total).
2. **4-byte little-endian length**: the size, in bytes, of the
   *decompressed* payload that follows (part 3, before compression).
3. **LZH-5-compressed payload** (the classic `-lh5-` method, the same
   compression the original uses for its own embedded skin resource).
   Once decompressed, that payload is:
   - a NUL-terminated author string,
   - a NUL-terminated comment string,
   - the raw bytes of your 358×150 BMP file.

**Known gap in this port:** `engine/src/util/lh5.c` only implements
LZH-5 *decompression* — the encoder was never ported (see that file's
own header comment). This means the C11 codebase alone can *load* a
custom skin but can't yet *build* the compressed container for you.
To produce the final `.ays`, you currently need one of:

- The original Windows `ay_emul` (or a compatible skin-editor tool) to
  compress your BMP + author/comment strings into this exact
  container, or
- A standalone LZH-5-capable encoder (the classic Unix/DOS `lha`
  archiver's `-lh5-` mode is compression-capable historically, but
  note the `lha`/`lhasa` packages on modern Ubuntu are
  **decompress-only reimplementations** — verify whatever encoder you
  use actually supports the `-lh5-` method before relying on it, and
  that you can extract just the raw compressed stream, not a full
  `.lzh` archive with its own separate header framing).

If you get stuck at this step, loading is still fully testable: unpack
an existing `.ays` (e.g. the bundled default skin) to confirm your
tool round-trips correctly, then swap in your own BMP before
re-compressing.

## Testing your skin

1. Launch `ay_emul_gui`.
2. Open **Tools → Change Skin**, pick your `.ays` file.
3. The window redraws immediately with your art. A bad/corrupt file
   is rejected with an error dialog and leaves the current skin
   untouched, so it's safe to experiment.
4. **Tools → Standard Skin** reverts to the embedded default at any
   time.

## Quick checklist

- [ ] Canvas is exactly 358×150, 24-bit BMP (or 32-bit with alpha -
      see [Alpha channel support](#alpha-channel-support-c11-only-backwards-compatible)).
- [ ] Every button has both a normal and (where listed above) a
      pressed sprite, both fully opaque (unless using real alpha).
- [ ] Both LEDs' "on" sprites are painted in their packed
      bottom-right locations.
- [ ] Both slider handles have a distinct, unused color in their
      top-left corner pixel for the transparency key (only needed if
      the bitmap has no real alpha channel).
- [ ] The ticker rect (108,48,197,24) is light/plain enough for gray
      text to stay readable.
- [ ] Author/comment strings are set (shown in Tools' skin info).
- [ ] Packaged with the exact 24-byte magic header + 4-byte
      decompressed-length + LZH-5 payload layout described above.
