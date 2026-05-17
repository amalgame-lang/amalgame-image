/*
 * Amalgame Standard Library — Amalgame.Image
 * Copyright (c) 2026 Bastien MOUGET
 * https://github.com/amalgame-lang/Amalgame
 *
 * Image decode + encode binding on top of Sean Barrett's public-
 * domain stb_image / stb_image_write / stb_image_resize2 single-
 * header libraries.
 *
 * Surface (v1):
 *   Load(path) / LoadFromBytes(bytes)            decode image file → handle
 *   IsValid(img) / LastError(img)                lifecycle + diag
 *   Width / Height / Channels                    geometry
 *   GetPixel(img, x, y)            -> int        packed 0xRRGGBBAA
 *   SetPixel(img, x, y, packed)                  in-place mutation
 *   SaveAsPng / SaveAsBmp / SaveAsTga / SaveAsJpg(quality)
 *                                                serialise back to disk
 *   Free(img)                                    no-op (GC owns the
 *                                                pixel buffer); kept
 *                                                for explicit ergonomics
 *
 * Surface (v0.2 additions):
 *   Create(w, h, packed)           -> AmalgameImage*    build from scratch
 *   Fill(img, packed)                                   solid colour
 *   GetR / GetG / GetB / GetA(x, y) -> int (0..255)     typed channel reads
 *   SetRGBA(x, y, r, g, b, a)                           typed channel writes
 *   Resize(img, newW, newH)        -> AmalgameImage*    Mitchell/cubic resample
 *   FlipH(img) / FlipV(img) / Rotate180(img)            in-place transforms
 *   Crop(img, x, y, w, h)          -> AmalgameImage*    sub-rect copy
 *
 * Format coverage (load): PNG, JPG, BMP, TGA, GIF (first frame),
 * PSD (composite), HDR, PIC, PNM.
 * Format coverage (save): PNG, BMP, TGA, JPG, HDR.
 *
 * Pixel layout: 8-bit RGBA, 4 channels regardless of source. We
 * force 4-channel decode (`desired_channels = 4`) so Amalgame sees
 * a uniform shape — saves a per-pixel branch in user code.
 *
 * Threading: AmalgameImage* is single-owner during mutation
 * (SetPixel / Save). Multiple concurrent readers on the same
 * handle are safe.
 *
 * Memory: stb_image allocates the pixel buffer via malloc; we
 * register a GC finalizer so `stbi_image_free` runs once the AM
 * handle becomes unreachable. The Free(img) verb is exposed for
 * users who want deterministic cleanup of large bitmaps without
 * waiting for the next GC sweep — it's optional, calling it
 * twice is safe (idempotent).
 */

#ifndef AMALGAME_IMAGE_H
#define AMALGAME_IMAGE_H

#include "_runtime.h"
#include "Amalgame_Collections.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include "stb_image_resize2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct AmalgameImage {
    unsigned char* pixels;     /* RGBA8, stbi_image_free'd via finalizer */
    int            width;      /* in pixels */
    int            height;     /* in pixels */
    int            channels;   /* source channels (1/3/4); pixel buffer is always 4 */
    char*          last_error; /* GC-strdup'd, or NULL */
    int            freed;      /* 1 once Free() has been called explicitly */
} AmalgameImage;

/* ── Small helpers ──────────────────────────────────── */

static inline code_string _amimg_err_dup(const char* msg) {
    if (!msg) return NULL;
    size_t n = strlen(msg);
    char* p = (char*) code_alloc(n + 1);
    memcpy(p, msg, n + 1);
    return p;
}

static void _amimg_finalize(void* obj, void* cd) {
    (void) cd;
    AmalgameImage* img = (AmalgameImage*) obj;
    if (img && img->pixels && !img->freed) {
        stbi_image_free(img->pixels);
        img->pixels = NULL;
        img->freed  = 1;
    }
}

static inline AmalgameImage* _amimg_alloc(void) {
    AmalgameImage* img = (AmalgameImage*) GC_MALLOC(sizeof(AmalgameImage));
    img->pixels     = NULL;
    img->width      = 0;
    img->height     = 0;
    img->channels   = 0;
    img->last_error = NULL;
    img->freed      = 0;
    GC_register_finalizer(img, _amimg_finalize, NULL, NULL, NULL);
    return img;
}

/* ── Load ───────────────────────────────────────────── */

static inline AmalgameImage* Amalgame_Image_Load(code_string path) {
    AmalgameImage* img = _amimg_alloc();
    if (!path) {
        img->last_error = _amimg_err_dup("null path");
        return img;
    }
    int w = 0, h = 0, n = 0;
    /* Force 4-channel decode so the pixel layout is uniform RGBA8. */
    unsigned char* px = stbi_load(path, &w, &h, &n, 4);
    if (!px) {
        img->last_error = _amimg_err_dup(stbi_failure_reason());
        return img;
    }
    img->pixels   = px;
    img->width    = w;
    img->height   = h;
    img->channels = n;
    return img;
}

/* Decode from an in-memory byte buffer. Convenient when the bytes
 * come from `File.ReadBytes` or an HTTP response; spares the temp
 * file dance. The List<int>* must hold 0..255 small ints. */
static inline AmalgameImage* Amalgame_Image_LoadFromBytes(AmalgameList* bytes) {
    AmalgameImage* img = _amimg_alloc();
    if (!bytes) {
        img->last_error = _amimg_err_dup("null bytes");
        return img;
    }
    i64 n = AmalgameList_count(bytes);
    if (n <= 0) {
        img->last_error = _amimg_err_dup("empty bytes");
        return img;
    }
    unsigned char* buf = (unsigned char*) GC_MALLOC((size_t) n);
    for (i64 i = 0; i < n; i++) {
        i64 v = (i64) (intptr_t) AmalgameList_get(bytes, i);
        buf[i] = (unsigned char) (v & 0xFF);
    }
    int w = 0, h = 0, c = 0;
    unsigned char* px = stbi_load_from_memory(buf, (int) n, &w, &h, &c, 4);
    if (!px) {
        img->last_error = _amimg_err_dup(stbi_failure_reason());
        return img;
    }
    img->pixels   = px;
    img->width    = w;
    img->height   = h;
    img->channels = c;
    return img;
}

/* ── Geometry + diag ────────────────────────────────── */

static inline code_bool Amalgame_Image_IsValid(AmalgameImage* img) {
    return (img && img->pixels && !img->freed) ? 1 : 0;
}

static inline code_string Amalgame_Image_LastError(AmalgameImage* img) {
    if (!img || !img->last_error) return (code_string) "";
    return img->last_error;
}

static inline i64 Amalgame_Image_Width(AmalgameImage* img) {
    return img ? (i64) img->width : 0;
}

static inline i64 Amalgame_Image_Height(AmalgameImage* img) {
    return img ? (i64) img->height : 0;
}

/* Number of channels in the *source* file (1=grey, 2=grey+alpha,
 * 3=RGB, 4=RGBA). The pixel buffer is always 4 channels regardless.
 * Exposed so users can branch on "did this file ship alpha?" when
 * round-tripping. */
static inline i64 Amalgame_Image_Channels(AmalgameImage* img) {
    return img ? (i64) img->channels : 0;
}

/* ── Pixel access ──────────────────────────────────── */

/* Read pixel at (x, y) as packed 32-bit: 0xRRGGBBAA (red high byte).
 * Out-of-bounds returns 0. */
static inline i64 Amalgame_Image_GetPixel(AmalgameImage* img, i64 x, i64 y) {
    if (!Amalgame_Image_IsValid(img)) return 0;
    if (x < 0 || y < 0 || x >= img->width || y >= img->height) return 0;
    size_t off = ((size_t) y * (size_t) img->width + (size_t) x) * 4;
    unsigned char* p = img->pixels + off;
    return ((i64) p[0] << 24) | ((i64) p[1] << 16) | ((i64) p[2] << 8) | (i64) p[3];
}

/* Write packed 0xRRGGBBAA at (x, y). Out-of-bounds is a silent no-op. */
static inline void Amalgame_Image_SetPixel(AmalgameImage* img,
                                             i64 x, i64 y, i64 packed) {
    if (!Amalgame_Image_IsValid(img)) return;
    if (x < 0 || y < 0 || x >= img->width || y >= img->height) return;
    size_t off = ((size_t) y * (size_t) img->width + (size_t) x) * 4;
    unsigned char* p = img->pixels + off;
    p[0] = (unsigned char) ((packed >> 24) & 0xFF);
    p[1] = (unsigned char) ((packed >> 16) & 0xFF);
    p[2] = (unsigned char) ((packed >>  8) & 0xFF);
    p[3] = (unsigned char) ( packed        & 0xFF);
}

/* ── Save ──────────────────────────────────────────── */

static inline code_bool Amalgame_Image_SaveAsPng(AmalgameImage* img, code_string path) {
    if (!Amalgame_Image_IsValid(img) || !path) return 0;
    int rc = stbi_write_png(path, img->width, img->height, 4,
                             img->pixels, img->width * 4);
    if (!rc) { img->last_error = _amimg_err_dup("PNG write failed"); return 0; }
    return 1;
}

static inline code_bool Amalgame_Image_SaveAsBmp(AmalgameImage* img, code_string path) {
    if (!Amalgame_Image_IsValid(img) || !path) return 0;
    int rc = stbi_write_bmp(path, img->width, img->height, 4, img->pixels);
    if (!rc) { img->last_error = _amimg_err_dup("BMP write failed"); return 0; }
    return 1;
}

static inline code_bool Amalgame_Image_SaveAsTga(AmalgameImage* img, code_string path) {
    if (!Amalgame_Image_IsValid(img) || !path) return 0;
    int rc = stbi_write_tga(path, img->width, img->height, 4, img->pixels);
    if (!rc) { img->last_error = _amimg_err_dup("TGA write failed"); return 0; }
    return 1;
}

/* quality is 1..100 ; clamped silently. JPG is lossy and drops the
 * alpha channel — round-tripping RGBA → JPG → load will return RGB. */
static inline code_bool Amalgame_Image_SaveAsJpg(AmalgameImage* img,
                                                   code_string path, i64 quality) {
    if (!Amalgame_Image_IsValid(img) || !path) return 0;
    int q = (int) quality;
    if (q < 1)   q = 1;
    if (q > 100) q = 100;
    int rc = stbi_write_jpg(path, img->width, img->height, 4, img->pixels, q);
    if (!rc) { img->last_error = _amimg_err_dup("JPG write failed"); return 0; }
    return 1;
}

/* Explicit cleanup. The GC finalizer will run this eventually
 * anyway; calling Free early helps when the user knows the bitmap
 * is dead but holds a reference for a while longer. Idempotent. */
static inline void Amalgame_Image_Free(AmalgameImage* img) {
    if (img && img->pixels && !img->freed) {
        stbi_image_free(img->pixels);
        img->pixels = NULL;
        img->freed  = 1;
    }
}

/* ═══════════════════════════════════════════════════════
 *  v0.2 — typed accessors + ops + Create/Fill
 * ═══════════════════════════════════════════════════════
 *
 * The v0.1 GetPixel/SetPixel packed-int API stays the canonical
 * way to read/write a whole pixel atomically; the per-channel
 * accessors below are a convenience layer over the same buffer.
 * Resize / Crop allocate fresh handles (with their own malloc'd
 * pixel buffer + finalizer), flip / rotate operate in place.
 */

/* ── Typed RGBA accessors ─────────────────────────────── */

static inline i64 Amalgame_Image_GetR(AmalgameImage* img, i64 x, i64 y) {
    if (!Amalgame_Image_IsValid(img)) return 0;
    if (x < 0 || y < 0 || x >= img->width || y >= img->height) return 0;
    size_t off = ((size_t) y * (size_t) img->width + (size_t) x) * 4;
    return (i64) img->pixels[off + 0];
}

static inline i64 Amalgame_Image_GetG(AmalgameImage* img, i64 x, i64 y) {
    if (!Amalgame_Image_IsValid(img)) return 0;
    if (x < 0 || y < 0 || x >= img->width || y >= img->height) return 0;
    size_t off = ((size_t) y * (size_t) img->width + (size_t) x) * 4;
    return (i64) img->pixels[off + 1];
}

static inline i64 Amalgame_Image_GetB(AmalgameImage* img, i64 x, i64 y) {
    if (!Amalgame_Image_IsValid(img)) return 0;
    if (x < 0 || y < 0 || x >= img->width || y >= img->height) return 0;
    size_t off = ((size_t) y * (size_t) img->width + (size_t) x) * 4;
    return (i64) img->pixels[off + 2];
}

static inline i64 Amalgame_Image_GetA(AmalgameImage* img, i64 x, i64 y) {
    if (!Amalgame_Image_IsValid(img)) return 0;
    if (x < 0 || y < 0 || x >= img->width || y >= img->height) return 0;
    size_t off = ((size_t) y * (size_t) img->width + (size_t) x) * 4;
    return (i64) img->pixels[off + 3];
}

/* Each component is masked to 0..255 (silently). Lets callers
 * compute pixel values in any int range without worrying about
 * overflow into adjacent channels. */
static inline void Amalgame_Image_SetRGBA(AmalgameImage* img,
                                            i64 x, i64 y,
                                            i64 r, i64 g, i64 b, i64 a) {
    if (!Amalgame_Image_IsValid(img)) return;
    if (x < 0 || y < 0 || x >= img->width || y >= img->height) return;
    size_t off = ((size_t) y * (size_t) img->width + (size_t) x) * 4;
    img->pixels[off + 0] = (unsigned char) (r & 0xFF);
    img->pixels[off + 1] = (unsigned char) (g & 0xFF);
    img->pixels[off + 2] = (unsigned char) (b & 0xFF);
    img->pixels[off + 3] = (unsigned char) (a & 0xFF);
}

/* ── Create + Fill ────────────────────────────────────── */

/* Allocate a fresh w × h RGBA image initialised to `packed`
 * (0xRRGGBBAA). Returns an image with IsValid=true and channels=4
 * unless w/h is non-positive (in which case the handle reports
 * a "bad dimensions" LastError + IsValid=false). The pixel buffer
 * is malloc'd through stb's allocator so the existing finalizer
 * (stbi_image_free) reclaims it. */
static inline AmalgameImage* Amalgame_Image_Create(i64 width, i64 height, i64 packed) {
    AmalgameImage* img = _amimg_alloc();
    if (width <= 0 || height <= 0) {
        img->last_error = _amimg_err_dup("Create: width/height must be > 0");
        return img;
    }
    size_t n = (size_t) width * (size_t) height * 4;
    unsigned char* px = (unsigned char*) malloc(n);
    if (!px) {
        img->last_error = _amimg_err_dup("Create: malloc failed");
        return img;
    }
    unsigned char r = (unsigned char) ((packed >> 24) & 0xFF);
    unsigned char g = (unsigned char) ((packed >> 16) & 0xFF);
    unsigned char b = (unsigned char) ((packed >>  8) & 0xFF);
    unsigned char a = (unsigned char) ( packed        & 0xFF);
    for (size_t i = 0; i < n; i += 4) {
        px[i + 0] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = a;
    }
    img->pixels   = px;
    img->width    = (int) width;
    img->height   = (int) height;
    img->channels = 4;
    return img;
}

/* Repaint every pixel of an existing image to `packed`. */
static inline void Amalgame_Image_Fill(AmalgameImage* img, i64 packed) {
    if (!Amalgame_Image_IsValid(img)) return;
    size_t n = (size_t) img->width * (size_t) img->height * 4;
    unsigned char r = (unsigned char) ((packed >> 24) & 0xFF);
    unsigned char g = (unsigned char) ((packed >> 16) & 0xFF);
    unsigned char b = (unsigned char) ((packed >>  8) & 0xFF);
    unsigned char a = (unsigned char) ( packed        & 0xFF);
    for (size_t i = 0; i < n; i += 4) {
        img->pixels[i + 0] = r;
        img->pixels[i + 1] = g;
        img->pixels[i + 2] = b;
        img->pixels[i + 3] = a;
    }
}

/* ── Resize / Crop (allocate fresh handles) ──────────── */

/* High-quality resampling via stb_image_resize2 (Mitchell on the
 * downsample path, cubic on upsample, clamps to edge). Returns a
 * new handle; the input is not mutated. */
static inline AmalgameImage* Amalgame_Image_Resize(AmalgameImage* src,
                                                     i64 newWidth, i64 newHeight) {
    AmalgameImage* out = _amimg_alloc();
    if (!Amalgame_Image_IsValid(src)) {
        out->last_error = _amimg_err_dup("Resize: invalid source image");
        return out;
    }
    if (newWidth <= 0 || newHeight <= 0) {
        out->last_error = _amimg_err_dup("Resize: width/height must be > 0");
        return out;
    }
    size_t n = (size_t) newWidth * (size_t) newHeight * 4;
    unsigned char* px = (unsigned char*) malloc(n);
    if (!px) {
        out->last_error = _amimg_err_dup("Resize: malloc failed");
        return out;
    }
    unsigned char* rc = stbir_resize_uint8_srgb(
        src->pixels, src->width, src->height, src->width * 4,
        px,          (int) newWidth, (int) newHeight, (int) newWidth * 4,
        STBIR_RGBA);
    if (!rc) {
        free(px);
        out->last_error = _amimg_err_dup("Resize: stbir_resize_uint8_srgb failed");
        return out;
    }
    out->pixels   = px;
    out->width    = (int) newWidth;
    out->height   = (int) newHeight;
    out->channels = 4;
    return out;
}

/* Sub-rectangle copy. (x, y) is the top-left of the crop window
 * in source coords; (w, h) is its size. Negative coords or a
 * window that escapes the source image clamp at the edge; if the
 * window is empty after clamping, the returned image carries a
 * "Crop: empty window" LastError. */
static inline AmalgameImage* Amalgame_Image_Crop(AmalgameImage* src,
                                                   i64 x, i64 y, i64 w, i64 h) {
    AmalgameImage* out = _amimg_alloc();
    if (!Amalgame_Image_IsValid(src)) {
        out->last_error = _amimg_err_dup("Crop: invalid source image");
        return out;
    }
    /* Clamp the window to the source rect. */
    i64 sx0 = x < 0 ? 0 : x;
    i64 sy0 = y < 0 ? 0 : y;
    i64 sx1 = x + w; if (sx1 > src->width)  sx1 = src->width;
    i64 sy1 = y + h; if (sy1 > src->height) sy1 = src->height;
    i64 cw = sx1 - sx0;
    i64 ch = sy1 - sy0;
    if (cw <= 0 || ch <= 0) {
        out->last_error = _amimg_err_dup("Crop: empty window");
        return out;
    }
    size_t n = (size_t) cw * (size_t) ch * 4;
    unsigned char* px = (unsigned char*) malloc(n);
    if (!px) {
        out->last_error = _amimg_err_dup("Crop: malloc failed");
        return out;
    }
    int stride = src->width * 4;
    for (i64 row = 0; row < ch; row++) {
        unsigned char* srow = src->pixels + ((sy0 + row) * stride) + (sx0 * 4);
        unsigned char* drow = px + (row * cw * 4);
        memcpy(drow, srow, (size_t) cw * 4);
    }
    out->pixels   = px;
    out->width    = (int) cw;
    out->height   = (int) ch;
    out->channels = 4;
    return out;
}

/* ── In-place transforms ─────────────────────────────── */

/* Horizontal flip — column x becomes column (width-1-x). */
static inline void Amalgame_Image_FlipH(AmalgameImage* img) {
    if (!Amalgame_Image_IsValid(img)) return;
    int w = img->width, h = img->height;
    int stride = w * 4;
    for (int y = 0; y < h; y++) {
        unsigned char* row = img->pixels + y * stride;
        for (int x = 0; x < w / 2; x++) {
            unsigned char* a = row + x * 4;
            unsigned char* b = row + (w - 1 - x) * 4;
            unsigned char tmp[4];
            memcpy(tmp, a, 4);
            memcpy(a,   b, 4);
            memcpy(b, tmp, 4);
        }
    }
}

/* Vertical flip — row y becomes row (height-1-y). */
static inline void Amalgame_Image_FlipV(AmalgameImage* img) {
    if (!Amalgame_Image_IsValid(img)) return;
    int w = img->width, h = img->height;
    int stride = w * 4;
    unsigned char* tmp = (unsigned char*) malloc((size_t) stride);
    if (!tmp) {
        img->last_error = _amimg_err_dup("FlipV: malloc failed");
        return;
    }
    for (int y = 0; y < h / 2; y++) {
        unsigned char* a = img->pixels + y * stride;
        unsigned char* b = img->pixels + (h - 1 - y) * stride;
        memcpy(tmp, a, (size_t) stride);
        memcpy(a,   b, (size_t) stride);
        memcpy(b, tmp, (size_t) stride);
    }
    free(tmp);
}

/* 180° rotation = FlipH + FlipV; doing it as one pass spares a
 * second buffer traversal. */
static inline void Amalgame_Image_Rotate180(AmalgameImage* img) {
    if (!Amalgame_Image_IsValid(img)) return;
    int w = img->width, h = img->height;
    size_t n = (size_t) w * (size_t) h;
    unsigned char* px = img->pixels;
    /* Pixel i ↔ pixel (n - 1 - i). Walk halves so each swap fires once. */
    for (size_t i = 0; i < n / 2; i++) {
        unsigned char* a = px + i * 4;
        unsigned char* b = px + (n - 1 - i) * 4;
        unsigned char tmp[4];
        memcpy(tmp, a, 4);
        memcpy(a,   b, 4);
        memcpy(b, tmp, 4);
    }
}

#endif /* AMALGAME_IMAGE_H */
