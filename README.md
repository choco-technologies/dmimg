# dmimg

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/dmimg/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/dmimg/actions/workflows/ci.yml)

The DMOD image decoder interface.

## Description

Every image format is a **decoder plugin**: a separate dmf module that
implements the dmimg DIF - `dmimg_png`, `dmimg_jpeg`, `dmimg_bmp`, ... A
plugin can live in any repository, public or private; a system contains the
decoders it needs, and a program that reads images does not depend on any of
them.

A program uses the dmimg API:

```c
#include "dmimg.h"

static int put(void* ctx, const dmimg_block_t* block)
{
    /* block->pixels: block->width x block->height 0xAARRGGBB pixels at
     * block->x, block->y of the image */
    return 0;
}

dmimg_info_t info;
dmimg_t image = dmimg_open_file("/sd/photo.jpg", &info, NULL);
if (image != NULL)
{
    uint8_t scale = (info.scales & DMIMG_SCALE(2)) ? 2 : 0;     /* 1/4 if the decoder can */
    dmimg_decode(image, scale, put, NULL);
    dmimg_close(image);
}
```

- `dmimg_open()` reads the first bytes of the image and asks the enabled
  decoders which of them recognizes it; `dmimg_open_file()` also loads the
  decoder named after the file's extension (`dmimg_jpeg` for `.jpg`) when no
  enabled one knows the file.
- Images are decoded in **blocks** of 0xAARRGGBB pixels - rows, or the
  8x8 / 16x16 blocks of JPEG. Neither the decoder nor dmimg keeps the whole
  image: what to do with the pixels (convert, scale, draw) is the program's.
- A decoder may decode at **1/2, 1/4, 1/8** of the size directly (JPEG): a
  large photo for a small screen costs a fraction of decoding it whole.

dmimg is used by **todmvi**, which converts images into dmview's `.dmvi`
files.

## Documentation

- [docs/api-reference.md](docs/api-reference.md) - the API and the DIF
- [docs/writing-a-decoder.md](docs/writing-a-decoder.md) - a decoder plugin, step by step

## Building

### Using CMake

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

Pass `-DDMOD_DIR=/path/to/local/dmod` to build against a local dmod checkout
instead of fetching `develop` from GitHub.

### Using Make

```bash
make DMOD_MODE=DMOD_MODULE DMOD_DIR=/path/to/dmod
```

## Testing

The tests use a decoder of their own, `dmimg_traw` (tests/traw/). Once built,
run them with `ctest`:

```bash
cd build
ctest --output-on-failure
```

## License

MIT - see [LICENSE](LICENSE).
