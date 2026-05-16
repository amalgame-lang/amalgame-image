/*
 * Amalgame Standard Library — Amalgame.Image
 * Copyright (c) 2026 Bastien MOUGET
 * https://github.com/amalgame-lang/Amalgame
 *
 * Image decode + encode binding on top of Sean Barrett's public-
 * domain stb_image / stb_image_write single-header libraries.
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

#endif /* AMALGAME_IMAGE_H */
