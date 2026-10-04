#define DMOD_ENABLE_REGISTRATION    ON
#define ENABLE_DIF_REGISTRATIONS    ON
#include "dmimg.h"
#include <errno.h>
#include <string.h>

/*
 * The dmimg API: finding the decoder of an image and handing the image to
 * it. The decoders are the enabled modules implementing the dmimg DIF;
 * each recognizes its format by the image's first bytes (_probe).
 *
 * Probing reads the start of the image. A seekable input is rewound for the
 * decoder; a forward-only one is replayed: the decoder reads the bytes
 * already read from a copy, then the rest from the input.
 */

#define IMAGE_MAGIC         0x494D4744u     /* 'DGMI' */
#define MODULE_PREFIX       "dmimg_"
#define MAX_EXTENSION       8u
#define NO_DECODER          1               /* Internal: no decoder recognized the image */

struct dmimg
{
    uint32_t                magic;
    Dmod_Context_t*         module;
    dmimg_decoder_t         decoder;
    dmod_dmimg_decode_t     decode;
    dmod_dmimg_close_t      close;
    uint8_t                 scales;
    bool                    decoded;

    const dmimg_input_t*    source;         /* The caller's input (or file_input) */
    dmimg_input_t           input;          /* What the decoder reads */
    uint8_t                 head[DMIMG_PROBE_SIZE];
    uint32_t                head_size;
    uint32_t                head_pos;       /* Replay: bytes of `head` given to the decoder */

    void*                   file;           /* dmimg_open_file() */
    dmimg_input_t           file_input;
};

/* Extensions that name another module than dmimg_<extension> */
static const struct
{
    char    extension[MAX_EXTENSION];
    char    format[MAX_EXTENSION];
} g_aliases[] = {
    { "jpg",  "jpeg" },
    { "jpe",  "jpeg" },
    { "jfif", "jpeg" },
    { "tif",  "tiff" },
};

/* ---- Inputs ---- */

/* static: their addresses are handed out as callbacks - a global function's
 * address would be taken through the GOT, which the dmod loader does not
 * relocate */
static int32_t replay_read(void* ctx, void* buffer, size_t size)
{
    struct dmimg* im = ctx;
    size_t done = 0;
    if (im->head_pos < im->head_size)
    {
        done = im->head_size - im->head_pos;
        if (done > size)
            done = size;
        memcpy(buffer, im->head + im->head_pos, done);
        im->head_pos += (uint32_t)done;
    }
    if (done == size)
        return (int32_t)done;
    int32_t n = im->source->read(im->source->ctx, (uint8_t*)buffer + done, size - done);
    return (n < 0) ? n : (int32_t)(done + (size_t)n);
}

static int32_t file_read(void* ctx, void* buffer, size_t size)
{
    return (int32_t)Dmod_FileRead(buffer, 1, size, ctx);
}

static int file_seek(void* ctx, uint32_t offset)
{
    return (Dmod_FileSeek(ctx, (Dmod_FileOffset_t)offset, DMOD_SEEK_SET) == 0) ? 0 : -EIO;
}

/* ---- Decoders ---- */

/* The next enabled decoder after `previous` that recognizes the head */
static Dmod_Context_t* find_decoder(const struct dmimg* im, Dmod_Context_t* previous)
{
    Dmod_Context_t* m = previous;
    while ((m = Dmod_GetNextDifModule(dmod_dmimg_probe_sig, m)) != NULL)
    {
        dmod_dmimg_probe_t probe = (dmod_dmimg_probe_t)Dmod_GetDifFunction(m, dmod_dmimg_probe_sig);
        if (probe != NULL && probe(im->head, im->head_size))
            return m;
    }
    return NULL;
}

/* Hand the image to decoder `m`: rewind (or replay) the input and open it */
static int start_decoder(struct dmimg* im, Dmod_Context_t* m, dmimg_info_t* info)
{
    dmod_dmimg_open_t open = (dmod_dmimg_open_t)Dmod_GetDifFunction(m, dmod_dmimg_open_sig);
    im->decode = (dmod_dmimg_decode_t)Dmod_GetDifFunction(m, dmod_dmimg_decode_sig);
    im->close = (dmod_dmimg_close_t)Dmod_GetDifFunction(m, dmod_dmimg_close_sig);
    if (open == NULL || im->decode == NULL || im->close == NULL)
    {
        DMOD_LOG_ERROR("dmimg: %s does not implement the whole decoder interface\n", Dmod_GetName(m));
        return -ENOTSUP;
    }

    if (im->source->seek != NULL)
    {
        if (im->source->seek(im->source->ctx, 0) != 0)
            return -EIO;
        im->input = *im->source;
    }
    else
    {
        im->head_pos = 0;
        im->input.read = replay_read;
        im->input.seek = NULL;
        im->input.ctx = im;
        im->input.size = im->source->size;
    }

    int status = 0;
    memset(info, 0, sizeof(*info));
    im->decoder = open(&im->input, info, &status);
    if (im->decoder == NULL)
        return (status != 0) ? status : -EBADMSG;
    info->scales |= DMIMG_SCALE(0);
    im->scales = info->scales;
    im->module = m;
    return 0;
}

/* dmimg_<format> for the extension of `path`: the module's name, or false */
static bool module_for(const char* path, char* name, size_t size)
{
    const char* dot = strrchr(path, '.');
    const char* slash = strrchr(path, '/');
    char extension[MAX_EXTENSION];
    size_t n = 0;
    if (dot == NULL || (slash != NULL && dot < slash) || dot[1] == '\0')
        return false;
    for (const char* p = dot + 1; *p != '\0'; p++)
    {
        if (n + 1U >= sizeof(extension))
            return false;
        extension[n++] = (*p >= 'A' && *p <= 'Z') ? (char)(*p - 'A' + 'a') : *p;
    }
    extension[n] = '\0';

    const char* format = extension;
    for (size_t i = 0; i < sizeof(g_aliases) / sizeof(g_aliases[0]); i++)
    {
        if (strcmp(g_aliases[i].extension, extension) == 0)
            format = g_aliases[i].format;
    }
    if (sizeof(MODULE_PREFIX) + strlen(format) > size)
        return false;
    memcpy(name, MODULE_PREFIX, sizeof(MODULE_PREFIX) - 1U);
    strcpy(name + sizeof(MODULE_PREFIX) - 1U, format);
    return true;
}

/* Load and enable the decoder named after the file's extension */
static bool load_decoder(const char* path)
{
    char name[sizeof(MODULE_PREFIX) + MAX_EXTENSION];
    if (!module_for(path, name, sizeof(name)))
        return false;
    if (Dmod_GetModuleContext(name) == NULL && Dmod_LoadModuleByName(name) == NULL)
        return false;
    return Dmod_IsModuleEnabled(name) || Dmod_EnableModule(name, false, NULL);
}

static int read_head(struct dmimg* im)
{
    im->head_size = 0;
    while (im->head_size < DMIMG_PROBE_SIZE)
    {
        int32_t n = im->source->read(im->source->ctx, im->head + im->head_size, DMIMG_PROBE_SIZE - im->head_size);
        if (n < 0)
            return -EIO;
        if (n == 0)
            break;
        im->head_size += (uint32_t)n;
    }
    return (im->head_size != 0) ? 0 : -EBADMSG;
}

/* The first decoder that recognizes the image and opens it - only one
 * tries a forward-only input: it cannot be replayed past the head */
static int try_decoders(struct dmimg* im, dmimg_info_t* info)
{
    int ret = NO_DECODER;
    for (Dmod_Context_t* m = find_decoder(im, NULL); m != NULL; m = find_decoder(im, m))
    {
        if ((ret = start_decoder(im, m, info)) == 0 || im->source->seek == NULL)
            return ret;
    }
    return ret;
}

/* Find the decoder - also by `path`'s extension, when there is one - and open the image */
static int open_image(struct dmimg* im, const char* path, dmimg_info_t* info)
{
    int ret = read_head(im);
    if (ret == 0 && (ret = try_decoders(im, info)) == NO_DECODER && path != NULL && load_decoder(path))
        ret = try_decoders(im, info);
    if (ret == NO_DECODER)
    {
        if (path != NULL)
            DMOD_LOG_WARN("dmimg: no decoder for %s\n", path);
        ret = -ENOTSUP;
    }
    return ret;
}

static void free_image(struct dmimg* im)
{
    if (im->decoder != NULL && im->close != NULL)
        im->close(im->decoder);
    if (im->file != NULL)
        Dmod_FileClose(im->file);
    im->magic = 0;
    Dmod_Free(im);
}

static dmimg_t finish_open(struct dmimg* im, int ret, int* status)
{
    if (status != NULL)
        *status = ret;
    if (ret == 0)
        return im;
    free_image(im);
    return NULL;
}

static struct dmimg* new_image(int* status)
{
    struct dmimg* im = Dmod_Malloc(sizeof(*im));
    if (im == NULL)
    {
        if (status != NULL)
            *status = -ENOMEM;
        return NULL;
    }
    memset(im, 0, sizeof(*im));
    im->magic = IMAGE_MAGIC;
    return im;
}

static bool is_image(dmimg_t image)
{
    return image != NULL && image->magic == IMAGE_MAGIC;
}

/* ---- API ---- */

dmod_dmimg_api_declaration(1.0, dmimg_t, _open, ( const dmimg_input_t* input, dmimg_info_t* info, int* status ))
{
    dmimg_info_t ignored;
    if (input == NULL || input->read == NULL)
    {
        if (status != NULL)
            *status = -EINVAL;
        return NULL;
    }
    struct dmimg* im = new_image(status);
    if (im == NULL)
        return NULL;
    im->source = input;
    return finish_open(im, open_image(im, NULL, (info != NULL) ? info : &ignored), status);
}

dmod_dmimg_api_declaration(1.0, dmimg_t, _open_file, ( const char* path, dmimg_info_t* info, int* status ))
{
    dmimg_info_t ignored;
    size_t size = 0;
    if (path == NULL)
    {
        if (status != NULL)
            *status = -EINVAL;
        return NULL;
    }
    struct dmimg* im = new_image(status);
    if (im == NULL)
        return NULL;
    if ((im->file = Dmod_FileOpen(path, "rb")) == NULL)
        return finish_open(im, -ENOENT, status);
    im->file_input.read = file_read;
    im->file_input.seek = file_seek;
    im->file_input.ctx = im->file;
    im->file_input.size = (Dmod_FileSizeToSizeT(Dmod_FileSize(im->file), &size) && size <= 0xFFFFFFFFu) ? (uint32_t)size : 0;
    im->source = &im->file_input;
    return finish_open(im, open_image(im, path, (info != NULL) ? info : &ignored), status);
}

dmod_dmimg_api_declaration(1.0, int, _decode, ( dmimg_t image, uint8_t scale, dmimg_output_fn output, void* ctx ))
{
    if (!is_image(image) || output == NULL || scale > DMIMG_MAX_SCALE || (image->scales & DMIMG_SCALE(scale)) == 0 ||
        image->decoded)
        return -EINVAL;
    image->decoded = true;
    return image->decode(image->decoder, scale, output, ctx);
}

dmod_dmimg_api_declaration(1.0, const char*, _decoder_name, ( dmimg_t image ))
{
    return is_image(image) ? Dmod_GetName(image->module) : NULL;
}

dmod_dmimg_api_declaration(1.0, void, _close, ( dmimg_t image ))
{
    if (is_image(image))
        free_image(image);
}

/* ---- Module ---- */

int dmod_init(const Dmod_Config_t* Config)
{
    (void)Config;
    return 0;
}

int dmod_deinit(void)
{
    return 0;
}
