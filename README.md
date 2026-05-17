# amalgame-image

Image decode + encode + transform for [Amalgame](https://github.com/amalgame-lang/Amalgame).
Wraps Sean Barrett's public-domain [stb_image / stb_image_write /
stb_image_resize2](https://github.com/nothings/stb) single-header
libraries (vendored at `runtime/vendor/`). No external dep at link
time — the three `.h` files ship with the package and are precompiled
once into a cached `.o` at `amc package add` time.

## Install

```bash
amc package add image                                      # via index
amc package add github.com/amalgame-lang/amalgame-image@v0.2.0
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

### v0.2.0 additions — typed accessors + ops + Create

| Method | Returns | Notes |
|---|---|---|
| `Image.Create(w, h, packed)` | `AmalgameImage*` | Allocate w×h RGBA filled with `packed` |
| `Image.Fill(img, packed)` | `void` | Repaint every pixel to `packed` |
| `Image.GetR(img, x, y)` | `int` (0..255) | Per-channel read |
| `Image.GetG(img, x, y)` | `int` (0..255) | Per-channel read |
| `Image.GetB(img, x, y)` | `int` (0..255) | Per-channel read |
| `Image.GetA(img, x, y)` | `int` (0..255) | Per-channel read |
| `Image.SetRGBA(img, x, y, r, g, b, a)` | `void` | Each component masked to 0..255 |
| `Image.Resize(img, newW, newH)` | `AmalgameImage*` | stb_image_resize2 (Mitchell/cubic, sRGB-aware) |
| `Image.Crop(img, x, y, w, h)` | `AmalgameImage*` | Sub-rect copy; clamps to edges |
| `Image.FlipH(img)` | `void` | Horizontal flip in place |
| `Image.FlipV(img)` | `void` | Vertical flip in place |
| `Image.Rotate180(img)` | `void` | 180° rotation in place |

```amalgame
// Build a 256×256 blue canvas from scratch, paint a red square,
// downsize to a thumbnail and save.
let canvas = Image.Create(256, 256, 65535)        // 0x0000FFFF
var y: int = 64
while (y < 192) {
    var x: int = 64
    while (x < 192) {
        Image.SetRGBA(canvas, x, y, 255, 0, 0, 255)
        x = x + 1
    }
    y = y + 1
}
let thumb = Image.Resize(canvas, 64, 64)
Image.SaveAsPng(thumb, "thumb.png")
```

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

## Deferred to v0.3+

- Multi-frame GIF / WebP / AVIF / TIFF (different decoder backend)
- Arbitrary-angle rotation, affine transforms, perspective warp
- Colour conversion (grayscale, sepia, HSV) + alpha compositing
- SIMD-accelerated paths (stb is portable C; stbir has SSE2/AVX/Neon
  but the Easy API used here doesn't expose them)
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
to BMP / TGA / JPG, and verify the round-trip. v0.2 adds 10 cases
exercising Create / Fill / typed RGBA accessors / Resize / Crop /
FlipH / FlipV / Rotate180.

## Per-OS install

The package itself has no system-level dep — `stb_image*` is pure C
vendored in the repo. The toolchain prereqs match the rest of the
Amalgame stdlib:

| OS / distro | Install |
|---|---|
| Debian / Ubuntu | `apt install gcc libgc-dev libcurl4-openssl-dev` |
| Fedora / RHEL | `dnf install gcc gc-devel libcurl-devel` |
| Arch / Manjaro | `pacman -S gcc gc curl` |
| Alpine | `apk add build-base gc-dev curl-dev` |
| macOS | `brew install bdw-gc curl` (Xcode CLT supplies the compiler) |
| Windows (MSYS2) | `pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-gc mingw-w64-x86_64-curl` |

## Licence

Apache-2.0 — see [`LICENSE`](LICENSE) and [`NOTICE.md`](NOTICE.md).
The vendored stb_image, stb_image_write and stb_image_resize2
headers are public domain (alternative MIT) — see the file
headers and `NOTICE.md`.
