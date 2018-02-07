//
// Created by alomaa on 2/1/2018.
//

#include "wrapper.h"

size_t cread(void *ptr, size_t size, size_t count, FILE *stream, long *cur) {
    size_t bytesRead = 0;

    fseek(stream, *cur, SEEK_SET);

    bytesRead = fread(ptr, size, count, stream);

    *cur += bytesRead;

    return bytesRead;
}

size_t cwrite(const void *ptr, size_t size, size_t count, FILE *stream, long *cur) {
    size_t bytesWritten = 0;

    fseek(stream, *cur, SEEK_SET);

    bytesWritten = fwrite(ptr, size, count, stream);

    *cur += bytesWritten;

    return bytesWritten;
}

int cseek(long *cur, long int offset, int origin) {
    if(origin == SEEK_SET){
        *cur = offset;
    }else if(origin == SEEK_CUR){
        *cur += offset;
    }
    return 0;
}
