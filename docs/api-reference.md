# dmimg API Reference

`#include "dmimg.h"`. Functions return 0 or a negative errno; a function
returning a handle reports its error through `status` (which may be NULL).

## Types

### `dmimg_input_t`

Where an image is read from - a file, memory, the network.

| Field | Meaning |
|-------|---------|
| `int32_t (*read)(void* ctx, void* buffer, size_t size)` | Bytes read - fewer than `size` only at the end - or < 0 on error |
| `int (*seek)(void* ctx, uint32_t offset)` | To `offset` from the start, 0 on success; NULL: the input is forward only |
| `void* ctx` | Passed to `read` and `seek` |
| `uint32_t size` | Size of the image, 0 when unknown |

Callbacks handed to another module must be `static` functions: the address
of a global function is taken through the GOT, which the dmod loader does
not relocate.

### `dmimg_info_t`

| Field | Meaning |
|-------|---------|
| `width`, `height` | Full size in pixels |
| `alpha` | The image may have translucent pixels |
| `scales` | `DMIMG_SCALE(n)` for every scale 1/2^n the decoder decodes at; always `DMIMG_SCALE(0)` |

### `dmimg_block_t`

Decoded pixels - a rectangle of the image at the scale it is decoded at
(`DMIMG_SCALED(width, n)` x `DMIMG_SCALED(height, n)`).

| Field | Meaning |
|-------|---------|
| `x`, `y`, `width`, `height` | The rectangle |
| `stride` | Pixels from one row of `pixels` to the next |
| `pixels` | 0xAARRGGBB, not premultiplied; valid only during the call |

Blocks come in any order and cover every pixel at least once; a later block
may draw over an earlier one (interlaced images).

### `dmimg_output_fn`

`int (*)(void* ctx, const dmimg_block_t* block)` - receives the blocks;
anything but 0 stops decoding, and the decode call returns that value.

## API

### `dmimg_open`

```c
dmimg_t dmimg_open(const dmimg_input_t* input, dmimg_info_t* info, int* status);
```

Read the first `DMIMG_PROBE_SIZE` bytes of the image, find the enabled
decoder that recognizes them and let it open the image. A seekable input is
rewound for the decoder; a forward-only one is replayed (the decoder reads
the bytes already read from a copy) - and only the first decoder that
recognizes it gets to try. `input` must stay valid until `dmimg_close()`.

Status: 0, `-ENOTSUP` (no decoder knows the image), `-EBADMSG` (empty), or
the decoder's error.

### `dmimg_open_file`

```c
dmimg_t dmimg_open_file(const char* path, dmimg_info_t* info, int* status);
```

`dmimg_open()` of a file. When no enabled decoder recognizes it, the module
named after the file's extension is loaded, enabled and asked too:
`dmimg_<extension>` in lower case, with the aliases `jpg`, `jpe`, `jfif` ->
`dmimg_jpeg` and `tif` -> `dmimg_tiff`. Status also `-ENOENT`.

### `dmimg_decode`

```c
int dmimg_decode(dmimg_t image, uint8_t scale, dmimg_output_fn output, void* ctx);
```

Decode the image at 1/2^scale of its size - one of `info.scales` - into
blocks for `output`. Once per image. Returns 0, what `output` returned to
stop, `-EINVAL` (a scale the decoder does not have, a second call), or the
decoder's error (`-EBADMSG` for a damaged image, `-EIO`, `-ENOMEM`).

### `dmimg_decoder_name`

```c
const char* dmimg_decoder_name(dmimg_t image);
```

Name of the module that decodes the image, e.g. `"dmimg_png"`.

### `dmimg_close`

```c
void dmimg_close(dmimg_t image);
```

Release the image, its decoder's state and the file `dmimg_open_file()`
opened. Safe on NULL.

## DIF - implemented by decoders

| Function | Meaning |
|----------|---------|
| `bool _probe(const uint8_t* head, size_t size)` | Whether the decoder recognizes the image by its first bytes (`DMIMG_PROBE_SIZE`, fewer when the image is shorter). Quick, no state. |
| `dmimg_decoder_t _open(const dmimg_input_t* input, dmimg_info_t* info, int* status)` | Read the header from `input` (at the start of the image, valid until `_close`), fill `info`. NULL with `*status` on failure: `-EBADMSG`, `-ENOTSUP` (a variant it does not have), `-EIO`, `-ENOMEM`. |
| `int _decode(dmimg_decoder_t decoder, uint8_t scale, dmimg_output_fn output, void* ctx)` | Decode at 1/2^scale (one of `info.scales`) into blocks, once. Return 0, what `output` returned, or an error. |
| `void _close(dmimg_decoder_t decoder)` | Release the state. |

See [writing-a-decoder.md](writing-a-decoder.md).
