#define DMOD_ENABLE_REGISTRATION    ON
#include "dmimg.h"
#include <errno.h>
#include <string.h>

/* "TRAW" images for the tests: magic, uint16_t width, height (LE), then
 * width x height 0xAARRGGBB pixels (LE). */

#define TRAW_HEADER     8u
#define MAX_WIDTH       64u

struct dmimg_decoder
{
    const dmimg_input_t*    input;
    uint32_t                width, height;
};

dmod_dmimg_dif_api_declaration(1.0, dmimg_traw, bool, _probe, ( const uint8_t* head, size_t size ))
{
    return size >= 4 && memcmp(head, "TRAW", 4) == 0;
}

dmod_dmimg_dif_api_declaration(1.0, dmimg_traw, dmimg_decoder_t, _open,
                               ( const dmimg_input_t* input, dmimg_info_t* info, int* status ))
{
    uint8_t h[TRAW_HEADER];
    if (input->read(input->ctx, h, sizeof(h)) != (int32_t)sizeof(h) || memcmp(h, "TRAW", 4) != 0)
    {
        *status = -EBADMSG;
        return NULL;
    }
    uint32_t w = (uint32_t)(h[4] | (h[5] << 8)), ht = (uint32_t)(h[6] | (h[7] << 8));
    if (w == 0 || ht == 0 || w > MAX_WIDTH)
    {
        *status = (w > MAX_WIDTH) ? -ENOTSUP : -EBADMSG;
        return NULL;
    }
    struct dmimg_decoder* d = Dmod_Malloc(sizeof(*d));
    if (d == NULL)
    {
        *status = -ENOMEM;
        return NULL;
    }
    d->input = input;
    d->width = w;
    d->height = ht;
    info->width = w;
    info->height = ht;
    info->alpha = true;
    info->scales = DMIMG_SCALE(0) | DMIMG_SCALE(1);
    return d;
}

dmod_dmimg_dif_api_declaration(1.0, dmimg_traw, int, _decode,
                               ( dmimg_decoder_t d, uint8_t scale, dmimg_output_fn output, void* ctx ))
{
    uint32_t row[MAX_WIDTH], out[2 * MAX_WIDTH];
    uint32_t step = 1u << scale, w = DMIMG_SCALED(d->width, scale);
    dmimg_block_t block;
    block.x = 0;
    block.width = w;
    block.stride = w;
    block.pixels = out;
    block.height = 0;

    for (uint32_t y = 0, oy = 0; y < d->height; y++)
    {
        uint8_t raw[MAX_WIDTH * 4];
        if (d->input->read(d->input->ctx, raw, d->width * 4u) != (int32_t)(d->width * 4u))
            return -EIO;
        if (y % step != 0)
            continue;
        for (uint32_t x = 0; x < d->width; x++)
            row[x] = (uint32_t)raw[x * 4] | ((uint32_t)raw[x * 4 + 1] << 8) | ((uint32_t)raw[x * 4 + 2] << 16) |
                         ((uint32_t)raw[x * 4 + 3] << 24);
        for (uint32_t x = 0; x < w; x++)
            out[block.height * w + x] = row[x * step];
        if (block.height++ == 0)
            block.y = oy;
        oy++;
        if (block.height == 2 || oy == DMIMG_SCALED(d->height, scale))
        {
            int ret = output(ctx, &block);
            if (ret != 0)
                return ret;
            block.height = 0;
        }
    }
    return 0;
}

dmod_dmimg_dif_api_declaration(1.0, dmimg_traw, void, _close, ( dmimg_decoder_t d ))
{
    Dmod_Free(d);
}

int dmod_init(const Dmod_Config_t* Config)
{
    (void)Config;
    return 0;
}

int dmod_deinit(void)
{
    return 0;
}
