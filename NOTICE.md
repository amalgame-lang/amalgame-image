# NOTICE — amalgame-image

## Authorship

Copyright 2026 Bastien Mouget. Original work — see
`runtime/Amalgame_Image.h`.

Part of the Amalgame ecosystem
([github.com/amalgame-lang/Amalgame](https://github.com/amalgame-lang/Amalgame)).
External contributions are paused at the ecosystem level; see the
main repo's `CONTRIBUTING.md` for the policy.

AI tools (Anthropic Claude) were used during development. Per
the project's authorship policy, AI is treated as a tool, not a
co-author at law.

## Licence

Apache License 2.0. See `LICENSE` for the full text.

## Third-party content

### stb_image — Sean Barrett

[stb_image.h v2.30](https://github.com/nothings/stb/blob/master/stb_image.h)
is vendored at `runtime/vendor/stb_image.h`. Public domain (alternative
MIT licence at the author's choice — see the file header).

> "I, Sean Barrett, dedicate this software to the public domain. […]
> ALTERNATIVE B - MIT License" — `stb_image.h`

Single-file C library that decodes PNG, JPG, BMP, TGA, GIF (first
frame), PSD (composite), HDR, PIC, PNM. ~283 KB / ~7700 LoC.
Upstream: [github.com/nothings/stb](https://github.com/nothings/stb).

### stb_image_write — Sean Barrett

[stb_image_write.h v1.16](https://github.com/nothings/stb/blob/master/stb_image_write.h)
is vendored at `runtime/vendor/stb_image_write.h`. Same dual-licence
(public domain / MIT) as stb_image.

> "I, Sean Barrett, dedicate this software to the public domain. […]
> ALTERNATIVE B - MIT License" — `stb_image_write.h`

Encodes PNG, BMP, TGA, JPG, HDR. ~71 KB / ~1700 LoC.

### stb_image_resize2 — Jeff Roberts & Jorge L Rodriguez (v0.2+)

[stb_image_resize2.h v2.18](https://github.com/nothings/stb/blob/master/stb_image_resize2.h)
is vendored at `runtime/vendor/stb_image_resize2.h`. Same dual-licence
(public domain / MIT) as stb_image.

> "I, Sean Barrett, dedicate this software to the public domain. […]
> ALTERNATIVE B - MIT License" — `stb_image_resize2.h`

Backs `Image.Resize`. SSE2/AVX/Neon/WASM SIMD paths under the hood;
we use the simple Easy API (`stbir_resize_uint8_srgb`). ~458 KB.

All three files travel with the package source; the user binary statically
links against the precompiled `stb_impl.o` produced at `amc package
add` time.

## Trademarks

None claimed.
