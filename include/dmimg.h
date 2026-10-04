#ifndef DMIMG_H
#define DMIMG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dmod.h"
#include "dmimg_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * dmimg - the image decoder interface. Every image format (PNG, JPEG, BMP,
 * ...) is a decoder plugin: a separate dmf module implementing the dmimg
 * DIF below, in any repository. A program that reads images (todmvi, a
 * photo viewer) uses the dmimg API: dmimg_open() asks the enabled decoders
 * which of them recognizes the image, and lets it decode the image in
 * blocks of 0xAARRGGBB pixels - neither the decoder nor dmimg keeps the
 * whole image.
 */

/** Bytes of the start of an image a decoder recognizes it by (fewer when the image is shorter). */
#define DMIMG_PROBE_SIZE        32u

/** Largest scale: an image is decoded at 1 / 2^scale of its size, 0 ... DMIMG_MAX_SCALE. */
#define DMIMG_MAX_SCALE         3u

/** Bit of dmimg_info_t.scales for 1 / 2^n. */
#define DMIMG_SCALE(n)          (1u << (n))

/** Width or height `size` decoded at 1 / 2^n (rounded up). */
#define DMIMG_SCALED(size, n)   (((size) + (1u << (n)) - 1u) >> (n))

/** A decoder's state for one image (owned by the decoder). */
typedef struct dmimg_decoder* dmimg_decoder_t;

/** An image opened with dmimg_open() - its decoder and the decoder's state. */
typedef struct dmimg* dmimg_t;

/**
 * Where an image is read from: a file, memory, the network. Reads are
 * sequential; a decoder that has to go back needs `seek`.
 */
typedef struct
{
    int32_t     (*read)(void* ctx, void* buffer, size_t size);  /**< Bytes read (< size only at the end), < 0 on error */
    int         (*seek)(void* ctx, uint32_t offset);            /**< To `offset` from the start, 0 on success; NULL: forward only */
    void*       ctx;
    uint32_t    size;                                           /**< Size of the image in bytes, 0 when unknown */
} dmimg_input_t;

/** What a decoder found out when it opened an image. */
typedef struct
{
    uint32_t    width;          /**< Full size, in pixels */
    uint32_t    height;
    bool        alpha;          /**< It may have translucent pixels */
    uint8_t     scales;         /**< DMIMG_SCALE(n) for every scale it decodes at - always DMIMG_SCALE(0) */
} dmimg_info_t;

/**
 * Decoded pixels: a rectangle of the image at the scale it is decoded at.
 * Blocks come in any order (rows of PNG, 8x8 / 16x16 blocks of JPEG) and
 * cover every pixel; a later block may draw over an earlier one (interlaced
 * images). The pixels are valid only during the call.
 */
typedef struct
{
    uint32_t        x;
    uint32_t        y;
    uint32_t        width;
    uint32_t        height;
    uint32_t        stride;     /**< Pixels from one row to the next */
    const uint32_t* pixels;     /**< 0xAARRGGBB, not premultiplied */
} dmimg_block_t;

/** Receives the decoded blocks; anything but 0 stops decoding with that value. */
typedef int (*dmimg_output_fn)(void* ctx, const dmimg_block_t* block);

/* ---- DIF - implemented by every decoder plugin ---- */

/**
 * @brief Whether the decoder recognizes an image by its first bytes.
 * @param head First DMIMG_PROBE_SIZE bytes (fewer when the image is shorter)
 */
dmod_dmimg_dif(1.0, bool, _probe, ( const uint8_t* head, size_t size ));

/**
 * @brief Start decoding an image: read its header.
 * The input is at the start of the image; it stays valid until _close().
 * @param status Receives 0, -EBADMSG (not an image of this format, damaged),
 *               -ENOTSUP (a variant the decoder does not have), -EIO, -ENOMEM
 * @return The decoder's state, NULL on failure
 */
dmod_dmimg_dif(1.0, dmimg_decoder_t, _open, ( const dmimg_input_t* input, dmimg_info_t* info, int* status ));

/**
 * @brief Decode the image at 1 / 2^scale of its size (one of info.scales)
 *        into blocks for @p output. Once per _open().
 * @return 0, what @p output returned to stop, or -EBADMSG / -EIO / -ENOMEM / -EINVAL
 */
dmod_dmimg_dif(1.0, int, _decode, ( dmimg_decoder_t decoder, uint8_t scale, dmimg_output_fn output, void* ctx ));

/** @brief Release the decoder's state. */
dmod_dmimg_dif(1.0, void, _close, ( dmimg_decoder_t decoder ));

/* ---- API - for programs that read images ---- */

/**
 * @brief Open an image: find the decoder that recognizes it among the
 *        enabled modules implementing the dmimg DIF.
 * @param input Valid until dmimg_close()
 * @param status Receives 0, -ENOTSUP (no decoder knows it), or the decoder's error (may be NULL)
 * @return The image, NULL on failure
 */
dmod_dmimg_api(1.0, dmimg_t, _open, ( const dmimg_input_t* input, dmimg_info_t* info, int* status ));

/**
 * @brief Open an image file. When no enabled decoder knows it, the module
 *        named after the file's extension - dmimg_<extension>, e.g.
 *        dmimg_png, dmimg_jpeg for .jpg - is loaded and asked too.
 * @param status Receives 0, -ENOENT, -ENOTSUP, or the decoder's error (may be NULL)
 */
dmod_dmimg_api(1.0, dmimg_t, _open_file, ( const char* path, dmimg_info_t* info, int* status ));

/**
 * @brief Decode an opened image at 1 / 2^scale (one of info.scales) into
 *        blocks for @p output. Once per image.
 */
dmod_dmimg_api(1.0, int, _decode, ( dmimg_t image, uint8_t scale, dmimg_output_fn output, void* ctx ));

/** @brief Name of the module that decodes the image, e.g. "dmimg_png". */
dmod_dmimg_api(1.0, const char*, _decoder_name, ( dmimg_t image ));

/** @brief Release an image (and close its file). Safe on NULL. */
dmod_dmimg_api(1.0, void, _close, ( dmimg_t image ));

#ifdef __cplusplus
}
#endif

#endif /* DMIMG_H */
