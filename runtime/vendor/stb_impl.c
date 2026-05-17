/*
 * stb_impl.c — single translation unit that materialises stb_image,
 * stb_image_write and (v0.2+) stb_image_resize2. All three headers
 * are header-only by default; defining the STB_*_IMPLEMENTATION
 * macros once across the whole program is what produces the
 * function bodies.
 *
 * Compiled once per platform under [stdlib].precompile=true and
 * cached at ~/.amalgame/packages/<...>/build/<platform>/. Final
 * link pulls the resulting .o into the user binary.
 *
 * All three upstream files are public-domain — see NOTICE.md for
 * the full attribution.
 */

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"
