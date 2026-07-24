// Implements the gzFile shim declared in zlib.h. Reads a whole file into
// memory and, if it turns out to be gzip-compressed, inflates it once with
// the ESP32 ROM's miniz (see hardware/rotor/build_micropython_fs.py, which
// gzip-compresses MSX cartridge dumps -- lowercase ".rom" -- to save flash
// space, but leaves BIOS dumps -- uppercase ".ROM" -- alone). No
// streaming/seeking of the compressed data itself: the decompressed result
// is buffered whole and served from there, which is what lets gzseek()
// behave like a normal fseek() despite gzip streams not being natively
// seekable, and MSX cartridge/BIOS dumps are small enough that buffering
// the whole thing in PSRAM is a non-issue.

#include "zlib.h"

#include <rg_system.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(ESP_PLATFORM) && ESP_IDF_VERSION_MAJOR < 5
#include <rom/miniz.h>
#else
#include <miniz.h>
#endif

// The real, path-resolved fopen() (see msxfix.c: it emulates a current
// directory, since fMSX relies on one and ESP-IDF has no such concept).
// gzfile_shim.c deliberately does not include msxfix.h, which would
// redefine fopen() (among others) to this same function -- calling it
// directly here instead keeps every other fopen/fread/fseek/fclose call in
// this file genuinely un-redefined.
extern FILE *msx_fopen(const char *path, const char *mode);

typedef struct
{
    uint8_t *data;
    size_t size;
    size_t pos;
} gz_buffer_t;

static void *read_whole_file(const char *path, size_t *out_len)
{
    FILE *fp = msx_fopen(path, "rb");
    if (!fp)
        return NULL;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    if (size < 0)
    {
        fclose(fp);
        return NULL;
    }
    fseek(fp, 0, SEEK_SET);

    void *buf = malloc((size_t)size);
    if (!buf)
    {
        fclose(fp);
        return NULL;
    }

    size_t n = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if (n != (size_t)size)
    {
        free(buf);
        return NULL;
    }

    *out_len = (size_t)size;
    return buf;
}

gzFile gzopen(const char *path, const char *mode)
{
    (void)mode; // read-only shim

    size_t raw_len = 0;
    void *raw = read_whole_file(path, &raw_len);
    if (!raw)
        return NULL;

    const uint8_t *bytes = raw;
    bool is_gzip = raw_len >= 10 && bytes[0] == 0x1F && bytes[1] == 0x8B && bytes[2] == 8;

    gz_buffer_t *f = malloc(sizeof(gz_buffer_t));
    if (!f)
    {
        free(raw);
        return NULL;
    }

    if (!is_gzip)
    {
        // Plain, already-uncompressed file (eg. a BIOS dump). Serve as-is.
        f->data = raw;
        f->size = raw_len;
        f->pos = 0;
        return f;
    }

    // Skip the 10-byte fixed header plus whatever optional fields FLG says
    // are present (RFC 1952). build_micropython_fs.py's gzip.compress()
    // call sets none of these, but parse them properly rather than assume.
    uint8_t flg = bytes[3];
    size_t off = 10;
    if (flg & 0x04) // FEXTRA
    {
        if (off + 2 > raw_len)
        {
            free(raw);
            free(f);
            return NULL;
        }
        uint16_t xlen = bytes[off] | (bytes[off + 1] << 8);
        off += 2 + xlen;
    }
    if (flg & 0x08) // FNAME
    {
        while (off < raw_len && bytes[off] != 0)
            off++;
        off++;
    }
    if (flg & 0x10) // FCOMMENT
    {
        while (off < raw_len && bytes[off] != 0)
            off++;
        off++;
    }
    if (flg & 0x02) // FHCRC
    {
        off += 2;
    }
    if (off + 8 >= raw_len)
    {
        printf("gzopen: header parsing ran past end of file\n");
        free(raw);
        free(f);
        return NULL;
    }

    // The gzip trailer's last 4 bytes are ISIZE: the uncompressed size mod
    // 2^32 (RFC 1952). Reading it lets us pre-size the output buffer and
    // decompress in one shot via the same lower-level, streaming
    // tinfl_decompress() rg_storage_unzip_file() already uses successfully
    // on this hardware -- tinfl_decompress_mem_to_heap(), the higher-level
    // one-shot wrapper, was tried first here and corrupted memory badly
    // enough to crash unrelated code (stdio) later on, for reasons not
    // fully understood; this streaming form is the one proven to work.
    const uint8_t *trailer = bytes + raw_len - 4;
    size_t out_size = (size_t)trailer[0] | ((size_t)trailer[1] << 8) |
                       ((size_t)trailer[2] << 16) | ((size_t)trailer[3] << 24);
    const uint8_t *crc_bytes = bytes + raw_len - 8;
    uint32_t expected_crc = (uint32_t)crc_bytes[0] | ((uint32_t)crc_bytes[1] << 8) |
                             ((uint32_t)crc_bytes[2] << 16) | ((uint32_t)crc_bytes[3] << 24);

    void *out = out_size ? malloc(out_size) : NULL;
    if (out_size && !out)
    {
        printf("gzopen: output buffer alloc failed (%u bytes)\n", (unsigned)out_size);
        free(raw);
        free(f);
        return NULL;
    }

    size_t in_size = raw_len - off;
    size_t decoded_size = out_size;
    tinfl_decompressor *decomp = malloc(sizeof(tinfl_decompressor));
    tinfl_status status = TINFL_STATUS_FAILED;
    if (decomp)
    {
        tinfl_init(decomp);
        status = tinfl_decompress(decomp, bytes + off, &in_size, out, out, &decoded_size,
                                   TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        free(decomp);
    }
    free(raw);

    if (status != TINFL_STATUS_DONE || decoded_size != out_size)
    {
        printf("gzopen: inflate failed (status=%d, got %u of %u bytes)\n",
               (int)status, (unsigned)decoded_size, (unsigned)out_size);
        free(out);
        free(f);
        return NULL;
    }

    uint32_t actual_crc = (uint32_t)mz_crc32(0, out, out_size);
    if (actual_crc != expected_crc)
    {
        printf("gzopen: CRC32 mismatch, expected %08x got %08x (%u bytes)\n",
               (unsigned)expected_crc, (unsigned)actual_crc, (unsigned)out_size);
        free(out);
        free(f);
        return NULL;
    }

    f->data = out;
    f->size = out_size;
    f->pos = 0;
    return f;
}

int gzread(gzFile file, void *buf, unsigned len)
{
    gz_buffer_t *f = file;
    if (!f)
        return -1;
    size_t remaining = f->size - f->pos;
    size_t n = len < remaining ? len : remaining;
    memcpy(buf, f->data + f->pos, n);
    f->pos += n;
    return (int)n;
}

int gzwrite(gzFile file, const void *buf, unsigned len)
{
    (void)file;
    (void)buf;
    (void)len;
    return 0;
}

char *gzgets(gzFile file, char *buf, int len)
{
    gz_buffer_t *f = file;
    if (!f || len <= 0 || f->pos >= f->size)
        return NULL;
    int i = 0;
    while (i < len - 1 && f->pos < f->size)
    {
        char c = ((char *)f->data)[f->pos++];
        buf[i++] = c;
        if (c == '\n')
            break;
    }
    buf[i] = 0;
    return buf;
}

long gzseek(gzFile file, long offset, int whence)
{
    gz_buffer_t *f = file;
    if (!f)
        return -1;
    // Real zlib's gzseek() does not support SEEK_END -- it returns -1
    // (errno=EINVAL) and leaves the position untouched, since a gzip stream
    // isn't seekable from the end without fully decompressing it. fMSX's own
    // code relies on exactly this failure mode: "if(!fseek(F,0,SEEK_END))
    // ... else { /* read forward in 16kB chunks to find the size */ }" (see
    // MSX.c, search "GZIPped"). Actually honoring SEEK_END here would make
    // that fallback start reading from the end instead of the beginning,
    // silently computing a size of 0.
    if (whence == SEEK_END)
        return -1;
    long newpos;
    if (whence == SEEK_SET)
        newpos = offset;
    else if (whence == SEEK_CUR)
        newpos = (long)f->pos + offset;
    else
        return -1;
    if (newpos < 0)
        newpos = 0;
    if ((size_t)newpos > f->size)
        newpos = (long)f->size;
    f->pos = (size_t)newpos;
    return newpos;
}

int gzrewind(gzFile file)
{
    gz_buffer_t *f = file;
    if (!f)
        return -1;
    f->pos = 0;
    return 0;
}

int gzgetc(gzFile file)
{
    gz_buffer_t *f = file;
    if (!f || f->pos >= f->size)
        return -1;
    return ((uint8_t *)f->data)[f->pos++];
}

long gztell(gzFile file)
{
    gz_buffer_t *f = file;
    return f ? (long)f->pos : -1;
}

int gzeof(gzFile file)
{
    gz_buffer_t *f = file;
    return f ? (f->pos >= f->size) : 1;
}

int gzclose(gzFile file)
{
    gz_buffer_t *f = file;
    if (f)
    {
        free(f->data);
        free(f);
    }
    return 0;
}
