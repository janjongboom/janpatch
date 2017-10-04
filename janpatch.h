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

// detect POSIX, and use FILE* in that case
#if !defined(JANPATCH_STREAM) && (defined (__unix__) || (defined (__APPLE__) && defined (__MACH__)))
#include <stdio.h>
#define JANPATCH_STREAM     FILE
#elif !defined(JANPATCH_STREAM)
#error "JANPATCH_STREAM not defined, and not on POSIX system. Please specify the JANPATCH_STREAM macro"
#endif

typedef struct {
    // fread/fwrite buffers
    char*    buffer;
    size_t   buffer_size;

    // function signatures
    int    (*getc)(JANPATCH_STREAM*);
    int    (*putc)(int, JANPATCH_STREAM*);
    size_t (*fread)(void*, size_t, size_t, JANPATCH_STREAM*);
    size_t (*fwrite)(const void*, size_t, size_t, JANPATCH_STREAM*);
    int    (*fseek)(JANPATCH_STREAM*, long int, int);
    long   (*ftell)(JANPATCH_STREAM*);
} janpatch_ctx;

enum {
    JANPATCH_OPERATION_ESC = 0xa7,
    JANPATCH_OPERATION_MOD = 0xa6,
    JANPATCH_OPERATION_INS = 0xa5,
    JANPATCH_OPERATION_DEL = 0xa4,
    JANPATCH_OPERATION_EQL = 0xa3,
    JANPATCH_OPERATION_BKT = 0xa2
};

static void process_mod(janpatch_ctx ctx, JANPATCH_STREAM *old, JANPATCH_STREAM *patch, JANPATCH_STREAM *target, bool up_old_stream) {
    // it can be that ESC character is actually in the data, but then it's prefixed with another ESC
    // so... we're looking for a lone ESC character
    size_t cnt = 0;
    while (1) {
        cnt++;
        int m = ctx.getc(patch);
        // JANPATCH_DEBUG("%02x ", m);
        // so... if it's *NOT* an ESC character, just write it to the target stream
        if (m != JANPATCH_OPERATION_ESC) {
            // JANPATCH_DEBUG("NOT ESC\n");
            ctx.putc(m, target);
            if (up_old_stream) {
                ctx.fseek(old, 1, SEEK_CUR); // and up old
            }
            continue;
        }

        // read the next character to see what we should do
        m = ctx.getc(patch);
        // JANPATCH_DEBUG("%02x ", m);

        // if the character after this is *not* an operator (except ESC)
        if (m == JANPATCH_OPERATION_ESC) {
            // JANPATCH_DEBUG("ESC, NEXT CHAR ALSO ESC\n");
            ctx.putc(m, target);
            if (up_old_stream) {
                ctx.fseek(old, 1, SEEK_CUR);
            }
        }
        else if (m >= 0xA2 && m <= 0xA6) { // character after this is an operator? Then roll back two characters and exit
            // JANPATCH_DEBUG("ESC, THEN OPERATOR\n");
            JANPATCH_DEBUG("%lu bytes\n", cnt);
            ctx.fseek(patch, -2, SEEK_CUR);
            break;
        }
        else { // else... write both the ESC and m
            // JANPATCH_DEBUG("ESC, BUT NO OPERATOR\n");
            ctx.putc(JANPATCH_OPERATION_ESC, target);
            ctx.putc(m, target);
            if (up_old_stream) {
                ctx.fseek(old, 2, SEEK_CUR); // up old by 2
            }
        }
    }
}

static int find_length(janpatch_ctx ctx, JANPATCH_STREAM *patch) {
    /* So... the EQL/BKT length thing works like this:
    *
    * If byte[0] is between 1..251 => use byte[0] + 1
    * If byte[0] is 252 => use ???
    * If byte[0] is 253 => use (byte[1] << 8) + byte[2]
    * If byte[0] is 254 => use (byte[1] << 16) + (byte[2] << 8) + byte[3] (NOT VERIFIED)
    */
    uint8_t l = ctx.getc(patch);
    if (l <= 251) {
        return l + 1;
    }
    else if (l == 252) {
        return l + ctx.getc(patch) + 1;
    }
    else if (l == 253) {
        return (ctx.getc(patch) << 8) + ctx.getc(patch);
    }
    else if (l == 254) {
        return (ctx.getc(patch) << 16) + (ctx.getc(patch) << 8) + (ctx.getc(patch));
    }
    else {
        JANPATCH_ERROR("EQL followed by unexpected byte %02x %02x\n", JANPATCH_OPERATION_EQL, l);
        return -1;
    }
}

int janpatch(janpatch_ctx ctx, JANPATCH_STREAM *old, JANPATCH_STREAM *patch, JANPATCH_STREAM *target) {

    int c;
    while ((c = ctx.getc(patch)) != EOF) {
        if (c == JANPATCH_OPERATION_ESC) {
            switch ((c = ctx.getc(patch))) {
                case JANPATCH_OPERATION_EQL: {
                    int length = find_length(ctx, patch);
                    if (length == -1) {
                        JANPATCH_ERROR("EQL length invalid\n");
                        JANPATCH_DEBUG("Positions are, old=%ld patch=%ld new=%ld\n", ctx.ftell(old), ctx.ftell(patch), ctx.ftell(target));
                        return 1;
                    }

                    JANPATCH_DEBUG("EQL: %d bytes\n", length);

                    int bytes_left = length;
                    while (bytes_left > 0) {
                        int read_size = ctx.buffer_size;
                        if (read_size > bytes_left) read_size = bytes_left;

                        // read into the buffer
                        int r = ctx.fread(ctx.buffer, 1, read_size, old);
                        if (r != read_size) {
                            JANPATCH_ERROR("fread returned %d, but expected %d\n", r, read_size);
                            return 1;
                        }

                        // write the buffer to the target JANPATCH_STREAM
                        r = ctx.fwrite(ctx.buffer, 1, read_size, target);
                        if (r != read_size) {
                            JANPATCH_ERROR("fwrite returned %d, but expected %d\n", r, read_size);
                            return 1;
                        }

                        bytes_left -= read_size;
                    }

                    break;
                }
                case JANPATCH_OPERATION_MOD: {
                    JANPATCH_DEBUG("MOD: ");

                    // MOD means to modify the next series of bytes
                    // so just write everything (until the next ESC sequence) to the target JANPATCH_STREAM
                    // but also up the position in the old JANPATCH_STREAM every time
                    process_mod(ctx, old, patch, target, true);
                    break;
                }
                case JANPATCH_OPERATION_INS: {
                    JANPATCH_DEBUG("INS: ");
                    // INS inserts the sequence in the new JANPATCH_STREAM, but does not up the position of the old JANPATCH_STREAM
                    // so just write everything (until the next ESC sequence) to the target JANPATCH_STREAM

                    process_mod(ctx, old, patch, target, false);
                    break;
                }
                case JANPATCH_OPERATION_BKT: {
                    // BKT = backtrace, seek back in old JANPATCH_STREAM with X bytes...
                    int length = find_length(ctx, patch);
                    if (length == -1) {
                        JANPATCH_ERROR("BKT length invalid\n");
                        JANPATCH_DEBUG("Positions are, old=%ld patch=%ld new=%ld\n", ctx.ftell(old), ctx.ftell(patch), ctx.ftell(target));
                        return 1;
                    }

                    JANPATCH_DEBUG("BKT: %d bytes\n", -length);

                    ctx.fseek(old, ctx.ftell(old) - length, SEEK_SET);

                    break;
                }
                case JANPATCH_OPERATION_DEL: {
                    // DEL deletes bytes, so up the old stream with X bytes
                    int length = find_length(ctx, patch);
                    if (length == -1) {
                        JANPATCH_ERROR("DEL length invalid\n");
                        JANPATCH_DEBUG("Positions are, old=%ld patch=%ld new=%ld\n", ctx.ftell(old), ctx.ftell(patch), ctx.ftell(target));
                        return 1;
                    }

                    JANPATCH_DEBUG("DEL: %d bytes\n", length);

                    ctx.fseek(old, length, SEEK_CUR);
                    break;
                }
                default: {
                    JANPATCH_ERROR("Unsupported operator %02x\n", c);
                    JANPATCH_DEBUG("Positions are, old=%ld patch=%ld new=%ld\n", ctx.ftell(old), ctx.ftell(patch), ctx.ftell(target));
                    return 1;
                }
            }
        }
        else {
            JANPATCH_ERROR("Expected ESC but got %02x\n", c);

            JANPATCH_DEBUG("Positions are, old=%ld patch=%ld new=%ld\n", ctx.ftell(old), ctx.ftell(patch), ctx.ftell(target));

            return 1;
        }
    }

    return 0;
}

#endif // _JANPATCH_H
