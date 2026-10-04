#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmimg.h"
#include <errno.h>
#include <string.h>

/*
 * dmimg with the test decoder dmimg_traw (traw/): "TRAW" images - the
 * magic, uint16_t width, height, then 0xAARRGGBB pixels. dmimg_open_file()
 * loads the decoder by the extension the first time.
 */

#ifndef DMIMG_TEST_DIR
#define DMIMG_TEST_DIR "."
#endif
#define TEST_FILE(name)     DMIMG_TEST_DIR "/" name

#define W   4
#define H   3

static uint8_t  g_file[256];
static size_t   g_file_size;
static uint32_t g_pixels[W * H];        /* What the decoder output */
static uint32_t g_covered;              /* Pixels output */
static uint32_t g_out_w;

static uint32_t pixel_at(uint32_t x, uint32_t y) { return 0xFF000000u | (y << 8) | x; }

/* A TRAW image of `w` x `h`, `pixels` of its pixels (fewer: truncated) */
static void make_traw(uint16_t w, uint16_t h, uint32_t pixels)
{
    memcpy(g_file, "TRAW", 4);
    g_file[4] = (uint8_t)w;
    g_file[5] = (uint8_t)(w >> 8);
    g_file[6] = (uint8_t)h;
    g_file[7] = (uint8_t)(h >> 8);
    g_file_size = 8;
    for (uint32_t i = 0; i < pixels; i++)
    {
        uint32_t p = pixel_at(i % w, i / w);
        if (i == 1)
            p = 0x80FFFFFFu;                /* (1, 0) translucent */
        memcpy(g_file + g_file_size, &p, 4);
        g_file_size += 4;
    }
}

static bool write_file(const char* path)
{
    void* f = Dmod_FileOpen(path, "wb");
    if (f == NULL)
        return false;
    bool ok = Dmod_FileWrite(g_file, 1, g_file_size, f) == g_file_size;
    Dmod_FileClose(f);
    return ok;
}

static int collect(void* ctx, const dmimg_block_t* b)
{
    (void)ctx;
    for (uint32_t y = 0; y < b->height; y++)
    {
        for (uint32_t x = 0; x < b->width; x++)
        {
            g_pixels[(b->y + y) * g_out_w + b->x + x] = b->pixels[y * b->stride + x];
            g_covered++;
        }
    }
    return 0;
}

static int stop(void* ctx, const dmimg_block_t* b)
{
    (void)ctx;
    (void)b;
    return 7;
}

/* An image in memory - forward only, or seekable */
typedef struct
{
    uint32_t pos;
} memory_t;

static int32_t memory_read(void* ctx, void* buffer, size_t size)
{
    memory_t* m = ctx;
    size_t n = (g_file_size - m->pos < size) ? g_file_size - m->pos : size;
    memcpy(buffer, g_file + m->pos, n);
    m->pos += (uint32_t)n;
    return (int32_t)n;
}

static int memory_seek(void* ctx, uint32_t offset)
{
    memory_t* m = ctx;
    if (offset > g_file_size)
        return -EINVAL;
    m->pos = offset;
    return 0;
}

static int decode(dmimg_t image, uint8_t scale, uint32_t out_w)
{
    memset(g_pixels, 0, sizeof(g_pixels));
    g_covered = 0;
    g_out_w = out_w;
    return dmimg_decode(image, scale, collect, NULL);
}

DMOD_TEST_STEP(dmimg_opens_a_file_with_the_decoder_of_its_extension)
{
    dmimg_info_t info;
    int status = -1;
    make_traw(W, H, W * H);
    DMOD_TEST_EXPECT_TRUE(write_file(TEST_FILE("a.TRAW")));

    dmimg_t image = dmimg_open_file(TEST_FILE("a.TRAW"), &info, &status);
    DMOD_TEST_EXPECT_TRUE(image != NULL);
    if (image == NULL)
        return;
    DMOD_TEST_EXPECT_EQ(status, 0);
    DMOD_TEST_EXPECT_EQ(info.width, W);
    DMOD_TEST_EXPECT_EQ(info.height, H);
    DMOD_TEST_EXPECT_TRUE(info.alpha);
    DMOD_TEST_EXPECT_EQ(info.scales, DMIMG_SCALE(0) | DMIMG_SCALE(1));
    DMOD_TEST_EXPECT_TRUE(strcmp(dmimg_decoder_name(image), "dmimg_traw") == 0);

    DMOD_TEST_EXPECT_EQ(dmimg_decode(image, 2, collect, NULL), -EINVAL);    /* not one of its scales */
    DMOD_TEST_EXPECT_EQ(decode(image, 0, W), 0);
    DMOD_TEST_EXPECT_EQ(g_covered, W * H);
    DMOD_TEST_EXPECT_EQ(g_pixels[0], pixel_at(0, 0));
    DMOD_TEST_EXPECT_EQ(g_pixels[1], 0x80FFFFFFu);
    DMOD_TEST_EXPECT_EQ(g_pixels[2 * W + 3], pixel_at(3, 2));
    DMOD_TEST_EXPECT_EQ(dmimg_decode(image, 0, collect, NULL), -EINVAL);    /* once */
    dmimg_close(image);
}

DMOD_TEST_STEP(dmimg_decodes_at_a_smaller_scale)
{
    dmimg_info_t info;
    make_traw(W, H, W * H);
    DMOD_TEST_EXPECT_TRUE(write_file(TEST_FILE("b.traw")));
    dmimg_t image = dmimg_open_file(TEST_FILE("b.traw"), &info, NULL);
    DMOD_TEST_EXPECT_TRUE(image != NULL);
    if (image == NULL)
        return;
    DMOD_TEST_EXPECT_EQ(decode(image, 1, DMIMG_SCALED(W, 1)), 0);
    DMOD_TEST_EXPECT_EQ(g_covered, 4u);                                   /* 2 x 2 */
    DMOD_TEST_EXPECT_EQ(g_pixels[0 * 2 + 1], pixel_at(2, 0));
    DMOD_TEST_EXPECT_EQ(g_pixels[1 * 2 + 0], pixel_at(0, 2));
    DMOD_TEST_EXPECT_EQ(g_pixels[1 * 2 + 1], pixel_at(2, 2));
    dmimg_close(image);
}

DMOD_TEST_STEP(dmimg_reads_any_input)
{
    dmimg_info_t info;
    memory_t m;
    dmimg_input_t input;
    make_traw(W, H, W * H);

    /* Forward only: the probed head is replayed to the decoder */
    m.pos = 0;
    input.read = memory_read;
    input.seek = NULL;
    input.ctx = &m;
    input.size = (uint32_t)g_file_size;
    dmimg_t image = dmimg_open(&input, &info, NULL);
    DMOD_TEST_EXPECT_TRUE(image != NULL);
    if (image != NULL)
    {
        DMOD_TEST_EXPECT_EQ(decode(image, 0, W), 0);
        DMOD_TEST_EXPECT_EQ(g_pixels[2 * W + 3], pixel_at(3, 2));
        dmimg_close(image);
    }

    /* Seekable: rewound */
    m.pos = 0;
    input.seek = memory_seek;
    image = dmimg_open(&input, &info, NULL);
    DMOD_TEST_EXPECT_TRUE(image != NULL);
    if (image != NULL)
    {
        DMOD_TEST_EXPECT_EQ(decode(image, 0, W), 0);
        DMOD_TEST_EXPECT_EQ(g_covered, W * H);
        DMOD_TEST_EXPECT_EQ(g_pixels[1 * W + 2], pixel_at(2, 1));
        DMOD_TEST_EXPECT_EQ(dmimg_decode(image, 0, NULL, NULL), -EINVAL);
        dmimg_close(image);
    }
}

DMOD_TEST_STEP(dmimg_reports_what_it_cannot_open)
{
    int status = 0;
    DMOD_TEST_EXPECT_TRUE(dmimg_open_file(TEST_FILE("missing.traw"), NULL, &status) == NULL);
    DMOD_TEST_EXPECT_EQ(status, -ENOENT);

    memcpy(g_file, "NOT AN IMAGE", 12);                 /* No decoder knows it */
    g_file_size = 12;
    DMOD_TEST_EXPECT_TRUE(write_file(TEST_FILE("c.qqq")));
    DMOD_TEST_EXPECT_TRUE(dmimg_open_file(TEST_FILE("c.qqq"), NULL, &status) == NULL);
    DMOD_TEST_EXPECT_EQ(status, -ENOTSUP);

    make_traw(100, 1, 0);                               /* The decoder's own error */
    DMOD_TEST_EXPECT_TRUE(write_file(TEST_FILE("d.traw")));
    DMOD_TEST_EXPECT_TRUE(dmimg_open_file(TEST_FILE("d.traw"), NULL, &status) == NULL);
    DMOD_TEST_EXPECT_EQ(status, -ENOTSUP);

    g_file_size = 0;                                    /* Empty */
    DMOD_TEST_EXPECT_TRUE(write_file(TEST_FILE("e.traw")));
    DMOD_TEST_EXPECT_TRUE(dmimg_open_file(TEST_FILE("e.traw"), NULL, &status) == NULL);
    DMOD_TEST_EXPECT_EQ(status, -EBADMSG);

    DMOD_TEST_EXPECT_TRUE(dmimg_open(NULL, NULL, &status) == NULL);
    DMOD_TEST_EXPECT_EQ(status, -EINVAL);
    dmimg_close(NULL);
}

DMOD_TEST_STEP(dmimg_passes_on_errors_while_decoding)
{
    make_traw(W, H, W * H - 2);                         /* Truncated */
    DMOD_TEST_EXPECT_TRUE(write_file(TEST_FILE("f.traw")));
    dmimg_t image = dmimg_open_file(TEST_FILE("f.traw"), NULL, NULL);
    DMOD_TEST_EXPECT_TRUE(image != NULL);
    DMOD_TEST_EXPECT_EQ(decode(image, 0, W), -EIO);
    DMOD_TEST_EXPECT_EQ(g_covered, 2u * W);             /* The rows before */
    dmimg_close(image);

    make_traw(W, H, W * H);                             /* The output stops it */
    DMOD_TEST_EXPECT_TRUE(write_file(TEST_FILE("g.traw")));
    image = dmimg_open_file(TEST_FILE("g.traw"), NULL, NULL);
    DMOD_TEST_EXPECT_TRUE(image != NULL);
    DMOD_TEST_EXPECT_EQ(dmimg_decode(image, 0, stop, NULL), 7);
    dmimg_close(image);
}
