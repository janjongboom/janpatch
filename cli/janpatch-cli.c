#include <stdio.h>
#include "janpatch.h"

int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) {
        printf("Usage: janpatch-cli [old-file] [patch-file] ([new-file])\n");
        return 1;
    }

    // Open streams
    FILE *old = fopen(argv[1], "rb");
    FILE *patch = fopen(argv[2], "rb");
    FILE *target = argc == 4 ? fopen(argv[3], "wb") : stdout;

    if (!old) {
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
        { (unsigned char*)malloc(1024), 1024 }, // source buffer
        { (unsigned char*)malloc(1024), 1024 }, // patch buffer
        { (unsigned char*)malloc(1024), 1024 }, // target buffer

        &fread,
        &fwrite,
        &fseek,
        &ftell
    };

    return janpatch(ctx, old, patch, target);
}
