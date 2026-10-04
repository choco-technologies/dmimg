# Writing a Decoder

A decoder is a dmf library module implementing the dmimg DIF. It can live
in its own repository - public or private - and needs only dmimg's headers.

## Name

Name the module **`dmimg_<format>`** after the format's usual file
extension: `dmimg_png`, `dmimg_bmp`, `dmimg_qoi`, `dmimg_jpeg` (`.jpg`,
`.jpe`, `.jfif` are aliases of `jpeg`), `dmimg_tiff` (`.tif`).
`dmimg_open_file()` then loads it on demand from the extension of a file no
enabled decoder recognizes. A decoder with another name works too, but
only when the system loads and enables it itself (e.g. in a `.dmd`).

## Build

```cmake
set(DMOD_MODULE_NAME        dmimg_qoi)
set(DMOD_DIF_IMPLS          dmimg)

dmod_add_library(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    src/qoi.c
)

dmod_link_modules(${DMOD_MODULE_NAME}
    dmimg@>=0.1
)
```

## Implementation

```c
#define DMOD_ENABLE_REGISTRATION    ON
#include "dmimg.h"
#include <errno.h>
#include <string.h>

struct dmimg_decoder
{
    const dmimg_input_t* input;
    uint32_t             width, height;
};

dmod_dmimg_dif_api_declaration(1.0, dmimg_qoi, bool, _probe, ( const uint8_t* head, size_t size ))
{
    return size >= 4 && memcmp(head, "qoif", 4) == 0;
}

dmod_dmimg_dif_api_declaration(1.0, dmimg_qoi, dmimg_decoder_t, _open,
                               ( const dmimg_input_t* input, dmimg_info_t* info, int* status ))
{
    /* Read the header with input->read(input->ctx, ...), check it,
     * allocate the state, fill info:
     *   info->width, info->height, info->alpha,
     *   info->scales = DMIMG_SCALE(0) (| DMIMG_SCALE(1) ... when it can) */
}

dmod_dmimg_dif_api_declaration(1.0, dmimg_qoi, int, _decode,
                               ( dmimg_decoder_t decoder, uint8_t scale, dmimg_output_fn output, void* ctx ))
{
    /* Read and decode; hand every row (or block) to output():
     *   dmimg_block_t b = { x, y, width, height, stride, pixels };
     *   int ret = output(ctx, &b);
     *   if (ret != 0) return ret;           - the caller stopped */
    return 0;
}

dmod_dmimg_dif_api_declaration(1.0, dmimg_qoi, void, _close, ( dmimg_decoder_t decoder ))
{
    Dmod_Free(decoder);
}

int dmod_init(const Dmod_Config_t* Config)  { (void)Config; return 0; }
int dmod_deinit(void)                       { return 0; }
```

The test decoder [tests/traw/traw.c](../tests/traw/traw.c) is a complete
example.

## Rules

- **Memory**: keep what the format needs - a row, a block, a dictionary -
  never the whole image unless the format requires it. The output buffer
  can be small: blocks of one or a few rows are fine.
- **Output** 0xAARRGGBB, not premultiplied; opaque pixels have alpha 0xFF.
  Set `info->alpha` when the image can have translucent pixels (a PNG with
  an alpha channel or tRNS) - a program may then keep them.
- **Scales**: offer `DMIMG_SCALE(n)` only when decoding smaller is cheaper
  than decoding whole (JPEG's DCT scaling). Output at scale n is
  `DMIMG_SCALED(width, n)` x `DMIMG_SCALED(height, n)`.
- **Input**: read sequentially. `input->seek` may be NULL (a forward-only
  stream); a decoder that cannot work without it fails `_open` with
  `-ENOTSUP` then.
- **Errors**: `-EBADMSG` for a damaged or truncated image, `-ENOTSUP` for a
  valid variant the decoder does not have, `-EIO` when reading fails,
  `-ENOMEM`. Never crash on a damaged image - every length and offset from
  the file is checked.
- **Probe** only by the magic bytes: quick, no allocation, no state.
