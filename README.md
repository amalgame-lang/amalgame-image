# amalgame-image

Image decode + encode for [Amalgame](https://github.com/amalgame-lang/Amalgame).
Wraps Sean Barrett's public-domain [stb_image](https://github.com/nothings/stb)
single-header libraries (vendored at `runtime/vendor/`). No external
dep at link time — `stb_image.h` and `stb_image_write.h` ship with
the package and are precompiled once into a cached `.o` at
`amc package add` time.

## Install

```bash
amc package add image                                      # via index
amc package add github.com/amalgame-lang/amalgame-image@v0.1.0
```

Requires **amc 0.5.4+** (precompile-on-install).

## Surface

```amalgame
import Amalgame.Image

let img = Image.Load("photo.png")
if (!Image.IsValid(img)) {
    Console.WriteLine("load failed: " + Image.LastError(img))
    return
}

Console.WriteLine("size: " + String_FromInt(Image.Width(img))
                + "x" + String_FromInt(Image.Height(img))
                + " (channels=" + String_FromInt(Image.Channels(img)) + ")")

// Read + mutate the top-left pixel (packed 0xRRGGBBAA).
let topLeft: int = Image.GetPixel(img, 0, 0)
Image.SetPixel(img, 0, 0, 0xFF000080)   // half-opaque red

// Save in PNG (round-trips alpha) or JPG (lossy, drops alpha).
Image.SaveAsPng(img, "out.png")
Image.SaveAsJpg(img, "out.jpg", 90)
```

### v0.1.0 method surface

| Method | Returns | Notes |
|---|---|---|
| `Image.Load(path)` | `AmalgameImage*` | PNG/JPG/BMP/TGA/GIF/PSD/HDR/PIC/PNM |
| `Image.LoadFromBytes(bytes)` | `AmalgameImage*` | Decode from `List<int>` (0..255) |
| `Image.IsValid(img)` | `bool` | False on load failure or after `Free` |
| `Image.LastError(img)` | `string` | Empty when no error |
| `Image.Width(img)` | `int` | Pixels |
| `Image.Height(img)` | `int` | Pixels |
| `Image.Channels(img)` | `int` | Source channel count (1/3/4); buffer is always RGBA8 |
| `Image.GetPixel(img, x, y)` | `int` | Packed `0xRRGGBBAA`; out-of-bounds returns 0 |
| `Image.SetPixel(img, x, y, packed)` | `void` | Out-of-bounds is silent no-op |
| `Image.SaveAsPng(img, path)` | `bool` | Lossless, full alpha |
| `Image.SaveAsBmp(img, path)` | `bool` | Lossless, 32-bit BMP |
| `Image.SaveAsTga(img, path)` | `bool` | Lossless |
| `Image.SaveAsJpg(img, path, q)` | `bool` | Lossy (q=1..100), drops alpha |
| `Image.Free(img)` | `void` | Optional; GC eventually does this anyway |

### Pixel layout

Pixels are 8-bit per channel RGBA, packed into `int` as
`0xRRGGBBAA` with red in the high byte. Regardless of the source
file's channel count (greyscale, RGB, or RGBA), the decoded buffer
is always 4 channels — `Channels()` reports the original count for
round-tripping decisions, but `GetPixel` / `SetPixel` always work
on RGBA8.

```
let pixel: int = Image.GetPixel(img, x, y)
let red:   int = (pixel >> 24) & 0xFF
let green: int = (pixel >> 16) & 0xFF
let blue:  int = (pixel >>  8) & 0xFF
let alpha: int =  pixel        & 0xFF
```

## Format coverage

**Load**: PNG, JPG, BMP, TGA, GIF (first frame only), PSD (composite
view), HDR (RGBE), PIC, PNM (PPM/PGM).

**Save**: PNG (lossless, alpha), BMP (32-bit), TGA, JPG (lossy,
quality 1–100, no alpha), HDR (float, via stb_image_write's RGBE
encoder — not exposed in v1).

## Deferred to v2

- Multi-frame GIF / WebP / AVIF / TIFF (different decoder backend)
- Resize / rotate / blit / colour conversion (consider sibling
  `amalgame-image-ops` package)
- SIMD-accelerated paths (stb is portable C)
- 16-bit per channel + HDR float pixel access
- Direct byte-buffer accessors (`AsList() : List<int>`) for inline
  pixel manipulation without GetPixel/SetPixel overhead

## Memory

`AmalgameImage*` wraps a malloc'd pixel buffer from stb_image; a GC
finalizer runs `stbi_image_free` once the handle becomes unreachable.
`Image.Free(img)` is optional — call it eagerly when you know a
large bitmap is dead and don't want to wait for the next GC sweep.
The verb is idempotent.

## Tests

```bash
./tests/run_tests.sh /path/to/amc
```

Tests are deterministic — no external fixture or server needed.
They write a tiny synthetic PNG, reload it, mutate one pixel, save
to BMP / TGA / JPG, and verify the round-trip.

## Licence

Apache-2.0 — see [`LICENSE`](LICENSE) and [`NOTICE.md`](NOTICE.md).
The vendored stb_image and stb_image_write headers are public
domain (alternative MIT) — see the file headers and `NOTICE.md`.
