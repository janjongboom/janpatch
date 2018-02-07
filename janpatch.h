#ifndef _JANPATCH_H
#define _JANPATCH_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef JANPATCH_DEBUG
#define JANPATCH_DEBUG(...)  while (0) {} // printf(__VA_ARGS__)
#endif

#ifndef JANPATCH_ERROR
#define JANPATCH_ERROR(...)  printf(__VA_ARGS__)
#endif

#define JANPATCH_STREAM FILE

typedef struct {
    unsigned char*   buffer;
    size_t           size;
    uint32_t         current_page;
    size_t           current_page_size;
    long         cur;
    JANPATCH_STREAM* stream;
} janpatch_buffer;

typedef struct {
    // fread/fwrite buffers
    janpatch_buffer source_buffer;
    janpatch_buffer patch_buffer;
    janpatch_buffer target_buffer;

    // function signatures
    size_t (*fread)(void*, size_t, size_t, JANPATCH_STREAM*, long*);
    size_t (*fwrite)(const void*, size_t, size_t, JANPATCH_STREAM*, long*);
    int    (*fseek)(long*, long int, int);

    // progress callback
    void   (*progress)(uint8_t);

    // the combination of the size of both the source + patch files (that's the max. the target file can be)
    long   max_file_size;
} janpatch_ctx;

enum {
    JANPATCH_OPERATION_ESC = 0xa7,
    JANPATCH_OPERATION_MOD = 0xa6,
    JANPATCH_OPERATION_INS = 0xa5,
    JANPATCH_OPERATION_DEL = 0xa4,
    JANPATCH_OPERATION_EQL = 0xa3,
    JANPATCH_OPERATION_BKT = 0xa2
};

/**
 * Get a character from the stream
 */
static int jp_getc(janpatch_ctx* ctx, janpatch_buffer* buffer) {
    long position = buffer->cur;
    if (position < 0) return -1;

    // calculate the current page...
    uint32_t page = ((unsigned long)position) / buffer->size;

    if (page != buffer->current_page) {
        ctx->fseek(&buffer->cur, page * buffer->size, SEEK_SET);
        buffer->current_page_size = ctx->fread(buffer->buffer, 1, buffer->size, buffer->stream, &buffer->cur);
        buffer->current_page = page;
    }

    int position_in_page = position % buffer->size;

    if (position_in_page >= buffer->current_page_size) {
        return EOF;
    }

    unsigned char b = buffer->buffer[position_in_page];
    ctx->fseek(&buffer->cur, position + 1, SEEK_SET);
    return b;
}

/**
 * Write a character to a stream
 */
static int jp_putc(int c, janpatch_ctx* ctx, janpatch_buffer* buffer) {
    long position = buffer->cur;
    if (position < 0) {
        return -1;
    }

    // calculate the current page...
    uint32_t page = ((unsigned long)position) / buffer->size;

    if (page != buffer->current_page) {
        // flush the page buffer...
        if (buffer->current_page != -1) {
            ctx->fseek(&buffer->cur, buffer->current_page * buffer->size, SEEK_SET);
            ctx->fwrite(buffer->buffer, 1, buffer->current_page_size, buffer->stream, &buffer->cur);

        }

        // and read the next page...
        ctx->fseek(&buffer->cur, page * buffer->size, SEEK_SET);
        ctx->fread(buffer->buffer, 1, buffer->size, buffer->stream, &buffer->cur);
        buffer->current_page_size = buffer->size;
        buffer->current_page = page;
    }

    int position_in_page = position % buffer->size;

    buffer->buffer[position_in_page] = (unsigned char)c;
    ctx->fseek(&buffer->cur, position + 1, SEEK_SET);

    return 0;
}

static void jp_final_flush(janpatch_ctx* ctx, janpatch_buffer* buffer) {
    long position = buffer->cur;
    size_t position_in_page = position % buffer->size;

    ctx->fseek(&buffer->cur, buffer->current_page * buffer->size, SEEK_SET);
    ctx->fwrite(buffer->buffer, 1, position_in_page, buffer->stream, &buffer->cur);

}

static void process_mod(janpatch_ctx *ctx, janpatch_buffer *source, janpatch_buffer *patch, janpatch_buffer *target, bool up_source_stream) {
    // it can be that ESC character is actually in the data, but then it's prefixed with another ESC
    // so... we're looking for a lone ESC character
    size_t cnt = 0;
    while (1) {
        cnt++;
        int m = jp_getc(ctx, patch);
        if (m == -1) {
            // End of file stream... rewind 1 character and return, this will yield back to janpatch main function, which will exit
            ctx->fseek(source->stream, -1, SEEK_CUR);
            return;
        }
        // JANPATCH_DEBUG("%02x ", m);
        // so... if it's *NOT* an ESC character, just write it to the target stream
        if (m != JANPATCH_OPERATION_ESC) {
            // JANPATCH_DEBUG("NOT ESC\n");
            jp_putc(m, ctx, target);
            if (up_source_stream) {
                ctx->fseek(&source->cur, 1, SEEK_CUR); // and up source
            }
            continue;
        }

        // read the next character to see what we should do
        m = jp_getc(ctx, patch);
        // JANPATCH_DEBUG("%02x ", m);

        if (m == -1) {
            // End of file stream... rewind 1 character and return, this will yield back to janpatch main function, which will exit
            ctx->fseek(source->stream, -1, SEEK_CUR);
            return;
        }

        // if the character after this is *not* an operator (except ESC)
        if (m == JANPATCH_OPERATION_ESC) {
            // JANPATCH_DEBUG("ESC, NEXT CHAR ALSO ESC\n");
            jp_putc(m, ctx, target);
            if (up_source_stream) {
                ctx->fseek(&source->cur, 1, SEEK_CUR);
            }
        }
        else if (m >= 0xA2 && m <= 0xA6) { // character after this is an operator? Then roll back two characters and exit
            // JANPATCH_DEBUG("ESC, THEN OPERATOR\n");
            JANPATCH_DEBUG("%lu bytes\n", cnt);
            ctx->fseek(&patch->cur, -2, SEEK_CUR);
            break;
        }
        else { // else... write both the ESC and m
            // JANPATCH_DEBUG("ESC, BUT NO OPERATOR\n");
            jp_putc(JANPATCH_OPERATION_ESC, ctx, target);
            jp_putc(m, ctx, target);
            if (up_source_stream) {
                ctx->fseek(&source->cur, 2, SEEK_CUR); // up source by 2
            }
        }
    }
}

static int find_length(janpatch_ctx *ctx, janpatch_buffer *buffer) {
    /* So... the EQL/BKT length thing works like this:
    *
    * If byte[0] is between 1..251 => use byte[0] + 1
    * If byte[0] is 252 => use ???
    * If byte[0] is 253 => use (byte[1] << 8) + byte[2]
    * If byte[0] is 254 => use (byte[1] << 16) + (byte[2] << 8) + byte[3] (NOT VERIFIED)
    */
    uint8_t l = jp_getc(ctx, buffer);
    if (l <= 251) {
        return l + 1;
    }
    else if (l == 252) {
        return l + jp_getc(ctx, buffer) + 1;
    }
    else if (l == 253) {
        return (jp_getc(ctx, buffer) << 8) + jp_getc(ctx, buffer);
    }
    else if (l == 254) {
        return (jp_getc(ctx, buffer) << 24) + (jp_getc(ctx, buffer) << 16) + (jp_getc(ctx, buffer) << 8) + (jp_getc(ctx, buffer));
    }
    else {
        JANPATCH_ERROR("EQL followed by unexpected byte %02x %02x\n", JANPATCH_OPERATION_EQL, l);
        return -1;
    }

    // it's fine if we get over the end of the stream here, will be caught by the next function
}

int janpatch(janpatch_ctx ctx, JANPATCH_STREAM *source, JANPATCH_STREAM *patch, JANPATCH_STREAM *target) {

    ctx.source_buffer.current_page = 0xffffffff;
    ctx.patch_buffer.current_page = 0xffffffff;
    ctx.target_buffer.current_page = 0xffffffff;

    ctx.source_buffer.stream = source;
    ctx.patch_buffer.stream = patch;
    ctx.target_buffer.stream = target;

    int c;
    while ((c = jp_getc(&ctx, &ctx.patch_buffer)) != EOF) {
        if (c == JANPATCH_OPERATION_ESC) {
            switch ((c = jp_getc(&ctx, &ctx.patch_buffer))) {
                case JANPATCH_OPERATION_EQL: {
                    int length = find_length(&ctx, &ctx.patch_buffer);
                    if (length == -1) {
                        JANPATCH_ERROR("EQL length invalid\n");
//                        JANPATCH_ERROR("Positions are, source=%ld patch=%ld new=%ld\n", ctx.ftell(source), ctx.ftell(patch), ctx.ftell(target));
                        return 1;
                    }

                    JANPATCH_DEBUG("EQL: %d bytes\n", length);

                    for (size_t ix = 0; ix < length; ix++) {
                        int r = jp_getc(&ctx, &ctx.source_buffer);
                        if (r < -1) {
                            JANPATCH_ERROR("fread returned %d, but expected character\n", r);
//                            JANPATCH_ERROR("Positions are, source=%ld patch=%ld new=%ld\n", ctx.ftell(source), ctx.ftell(patch), ctx.ftell(target));
                            return 1;
                        }

                        jp_putc(r, &ctx, &ctx.target_buffer);
                    }

                    break;
                }
                case JANPATCH_OPERATION_MOD: {
                    JANPATCH_DEBUG("MOD: ");

                    // MOD means to modify the next series of bytes
                    // so just write everything (until the next ESC sequence) to the target JANPATCH_STREAM
                    // but also up the position in the source JANPATCH_STREAM every time
                    process_mod(&ctx, &ctx.source_buffer, &ctx.patch_buffer, &ctx.target_buffer, true);
                    break;
                }
                case JANPATCH_OPERATION_INS: {
                    JANPATCH_DEBUG("INS: ");
                    // INS inserts the sequence in the new JANPATCH_STREAM, but does not up the position of the source JANPATCH_STREAM
                    // so just write everything (until the next ESC sequence) to the target JANPATCH_STREAM

                    process_mod(&ctx, &ctx.source_buffer, &ctx.patch_buffer, &ctx.target_buffer, false);
                    break;
                }
                case JANPATCH_OPERATION_BKT: {
                    // BKT = backtrace, seek back in source JANPATCH_STREAM with X bytes...
                    int length = find_length(&ctx, &ctx.patch_buffer);
                    if (length == -1) {
                        JANPATCH_ERROR("BKT length invalid\n");
//                        JANPATCH_ERROR("Positions are, source=%ld patch=%ld new=%ld\n", ctx.ftell(source), ctx.ftell(patch), ctx.ftell(target));
                        return 1;
                    }

                    JANPATCH_DEBUG("BKT: %d bytes\n", -length);

                    ctx.fseek(&ctx.source_buffer.cur, ctx.source_buffer.cur - length, SEEK_SET);

                    break;
                }
                case JANPATCH_OPERATION_DEL: {
                    // DEL deletes bytes, so up the source stream with X bytes
                    int length = find_length(&ctx, &ctx.patch_buffer);
                    if (length == -1) {
                        JANPATCH_ERROR("DEL length invalid\n");
//                        JANPATCH_ERROR("Positions are, source=%ld patch=%ld new=%ld\n", ctx.ftell(source), ctx.ftell(patch), ctx.ftell(target));
                        return 1;
                    }

                    JANPATCH_DEBUG("DEL: %d bytes\n", length);

                    ctx.fseek(&ctx.source_buffer.cur, length, SEEK_CUR);
                    break;
                }
                case -1: {
                    // End of file stream... rewind 1 character and break, this will yield back to main loop
                    ctx.fseek(source, -1, SEEK_CUR);
                    break;
                }
                default: {
                    JANPATCH_ERROR("Unsupported operator %02x\n", c);
//                    JANPATCH_ERROR("Positions are, source=%ld patch=%ld new=%ld\n", ctx.ftell(source), ctx.ftell(patch), ctx.ftell(target));
                    return 1;
                }
            }
        }
        else {
            JANPATCH_ERROR("Expected ESC but got %02x\n", c);
//            JANPATCH_ERROR("Positions are, source=%ld patch=%ld new=%ld\n", ctx.ftell(source), ctx.ftell(patch), ctx.ftell(target));

            return 1;
        }
    }

    jp_final_flush(&ctx, &ctx.target_buffer);

    return 0;
}

#endif // _JANPATCH_H
