#include <stdio.h>
#include "janpatch.h"
#include "wrapper.h"

#define BUFFERSIZE 1024

static uint8_t source_buffer[BUFFERSIZE];
static uint8_t patch_buffer[BUFFERSIZE];
static uint8_t target_buffer[BUFFERSIZE];


int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) {
        printf("Usage: janpatch-cli [old-file] [patch-file] ([new-file])");
        return 1;
    }

    // Open streams
    FILE *source = fopen(argv[1], "rb");
    FILE *patch = fopen(argv[2], "rb");
    FILE *target = argc == 4 ? fopen(argv[3], "wb") : stdout;

    if (!source) {
        printf("Could not open '%s'\n", argv[1]);
        return 1;
    }
    if (!patch) {
        printf("Could not open '%s'\n", argv[2]);
        return 1;
    }
    if (!target) {
        printf("Could not open '%s'\n", argv[3]);
        return 1;
    }

    // janpatch_ctx contains buffers, and references to the file system functions
    janpatch_ctx ctx = {
        { source_buffer, BUFFERSIZE }, // source buffer
        { patch_buffer, BUFFERSIZE }, // patch buffer
        { target_buffer, BUFFERSIZE }, // target buffer

        &cread,
        &cwrite,
        &cseek,
    };

    return janpatch(ctx, source, patch, target);
}
