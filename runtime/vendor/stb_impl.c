/*
 * stb_impl.c — single translation unit that materialises stb_image
 * and stb_image_write. Both headers are header-only by default;
 * defining the two STB_*_IMPLEMENTATION macros once across the
 * whole program is what produces the function bodies.
 *
 * Compiled once per platform under [stdlib].precompile=true and
 * cached at ~/.amalgame/packages/<...>/build/<platform>/. Final
 * link pulls the resulting .o into the user binary.
 *
 * Both upstream files are public-domain — see NOTICE.md for the
 * full attribution.
 */

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
